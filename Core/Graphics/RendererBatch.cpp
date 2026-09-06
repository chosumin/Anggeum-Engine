#include "stdafx.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Material.h"
#include "Graphics/SubMesh.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/ResourceManager.h"
#include "Foundation/Entity.h"
#include "Foundation/Scene.h"
#include "Components/Mesh.h"
#include "Components/Transform.h"

#include "Graphics/BufferUpload.h"
#include "Graphics/FrameResources.h"

using namespace Core;

Core::RendererBatch::RendererBatch(Device& device, ResourceManager& resourceManager)
    : _device(device), _resourceManager(resourceManager)
{
}

Core::RendererBatch::~RendererBatch() = default;

void Core::RendererBatch::AddMesh(uint entityId, Handle<Material> material, Handle<SubMesh> subMesh)
{
    auto* materialPtr = material.TryGet();
    if (!materialPtr)
        return;

    auto materialName = materialPtr->GetName();

    auto matPtr = _materialBatches.find(materialName);
    if (matPtr == _materialBatches.end())
    {
        MaterialBatch materialBatch;
        materialBatch.Material = material;
        _materialBatches.insert(make_pair(materialName, materialBatch));
    }

    auto& subMeshBatches = _materialBatches[materialName].SubMeshBatches;

    auto* subMeshPtr = subMesh.TryGet();
    if (!subMeshPtr)
        return;

    string subMeshName = subMeshPtr->GetName();
    auto& subMeshBatch = subMeshBatches[subMeshName];

    if (subMeshBatch.Transforms.empty())
        subMeshBatch.SubMesh = subMesh;

    subMeshBatch.Transforms.push_back(entityId);
}

void Core::RendererBatch::InitializeFromScene(Scene& scene)
{
    auto meshes = scene.GetComponents<Mesh>();

    for (auto* mesh : meshes)
    {
        uint entityId = mesh->GetEntity().GetId();
        glm::mat4 world = mesh->GetEntity().GetTransform().GetMatrix();

        auto& materials = mesh->GetMaterials();
        auto& subMeshes = mesh->GetSubMeshes();

        for (size_t i = 0; i < materials.size(); ++i)
        {
            if (i >= subMeshes.size())
                break;

            auto shaderHandle = materials[i].Get().GetShaderHandle();
            if (!shaderHandle.IsValid())
                continue;

            // Skip non-geometry passes (Skybox, etc.)
            const string& pass = shaderHandle.Get().GetPass();
            if (pass != "Geometry")
                continue;

            _transforms[entityId] = world;
            AddMesh(entityId, materials[i], subMeshes[i]);
        }
    }
}

Handle<Buffer> Core::RendererBatch::AcquirePersistentBuffer(Handle<Buffer> current,
    const BufferDesc& desc, const string& name)
{
    if (current.IsValid())
    {
        if (current.Get().GetSize() == desc.size)
            return current;

        _resourceManager.ResizeBuffer(current, desc, name);

        return current;
    }

    return _resourceManager.LoadBuffer(desc, name);
}

template<typename T>
static vector<uint8_t> ToBytes(const T* data, size_t count)
{
    vector<uint8_t> bytes(count * sizeof(T));
    memcpy(bytes.data(), data, bytes.size());
    return bytes;
}

uint32_t Core::RendererBatch::AppendDraw(Handle<Material> material,
    Handle<SubMesh> subMesh, const vector<uint>& transforms)
{
    const uint32_t cmdSlot = _indirectDrawBuffer.GetDrawCount();
    const uint32_t firstInstance = _instanceCount;
    assert(cmdSlot < _commandCapacity
        && firstInstance + transforms.size() <= _instanceCapacity);

    const auto& allocation = subMesh.Get().GetAllocation();
    _indirectDrawBuffer.AddDrawCommand(allocation,
        material.Get().GetMaterialIndex(), 0, firstInstance);

    for (uint entityId : transforms)
    {
        GPUObjectData data{};
        data.boundingSphere = glm::vec4(
            allocation.boundingSphereCenter, allocation.boundingSphereRadius);
        data.transformIndex = entityId;
        data.drawCommandIndex = cmdSlot;
        _objectData.push_back(data);
    }

    _instanceCount += static_cast<uint>(transforms.size());
    return cmdSlot;
}

void Core::RendererBatch::AppendPendingResident()
{
    if (_pendingDraws.empty())
        return;

    bool appended = false;
    for (auto it = _pendingDraws.begin(); it != _pendingDraws.end();)
    {
        PendingDraw& pending = it->second;
        auto* subMesh = pending.SubMesh.TryGet();
        if (!subMesh)
        {
            it = _pendingDraws.erase(it);
            continue;
        }

        auto* material = pending.Material.TryGet();
        if (!material || !material->HasMaterialIndex()
            || !subMesh->HasAllocation() || !pending.SubMesh.IsResident())
        {
            ++it;
            continue;
        }

        const uint32_t firstInstance = _instanceCount;
        const uint32_t cmdSlot = AppendDraw(pending.Material, pending.SubMesh,
            pending.Transforms);

        // This draw's own table entries, written at their final offsets.
        _pendingTableFills.push_back({ _indirectCommandBuffer,
            ToBytes(&_indirectDrawBuffer.GetDrawCommands()[cmdSlot], 1),
            cmdSlot * sizeof(DrawIndexedIndirectCommand) });
        _pendingTableFills.push_back({ _materialIndexBuffer,
            ToBytes(&_indirectDrawBuffer.GetMaterialIndices()[cmdSlot], 1),
            cmdSlot * sizeof(uint32_t) });
        _pendingTableFills.push_back({ _objectDataBuffer,
            ToBytes(&_objectData[firstInstance], pending.Transforms.size()),
            firstInstance * sizeof(GPUObjectData) });

        appended = true;
        it = _pendingDraws.erase(it);
    }

    if (appended)
        ++_revision;
}

void Core::RendererBatch::RebuildGpuBuffers()
{
    // Bumped before the early-out below so an empty rebuild still counts: the draw
    // set changed either way, and Cullers have to notice.
    ++_revision;

    _indirectDrawBuffer.Clear();
    _objectData.clear();
    _pendingDraws.clear();
    _pendingTableFills.clear();
    _instanceCount = 0;

    // --- Transform buffer (indexed by entity id) ---
    uint32_t maxEntityId = 0;
    bool anyTransform = !_transforms.empty();
    for (const auto& kv : _transforms)
        maxEntityId = std::max(maxEntityId, static_cast<uint32_t>(kv.first));

    vector<mat4> transforms;
    if (anyTransform)
    {
        transforms.assign(static_cast<size_t>(maxEntityId) + 1, mat4(1.0f));
        for (const auto& [entityId, matrix] : _transforms)
            transforms[entityId] = matrix;

        BufferDesc transformDesc{};
        transformDesc.size = transforms.size() * sizeof(mat4);
        transformDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        _transformBuffer = AcquirePersistentBuffer(
            _transformBuffer, transformDesc, "RendererBatch.Transform");
    }

    // Capacity covers the whole membership: promotions only move entries from
    // pending into the live prefix, so appends can never overflow.
    _commandCapacity = 0;
    _instanceCapacity = 0;
	for (const auto& [materialName, materialBatch] : _materialBatches)
	{
		for (const auto& [subMeshName, subMeshBatch] : materialBatch.SubMeshBatches)
		{
			++_commandCapacity;
			_instanceCapacity += static_cast<uint32_t>(subMeshBatch.Transforms.size());
		}
	}

    // Nothing to draw: keep whatever (possibly empty) buffers exist; the draw paths
    // early-out on GetDrawCommandCount() == 0.
    if (_commandCapacity == 0)
    {
        _hasGpuBuffers = true;
        return;
    }

    BufferDesc indirectDesc{};
    indirectDesc.size = _commandCapacity * sizeof(DrawIndexedIndirectCommand);
    indirectDesc.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    _indirectCommandBuffer = AcquirePersistentBuffer(
        _indirectCommandBuffer, indirectDesc, "RendererBatch.IndirectCommand");

    BufferDesc materialDesc{};
    materialDesc.size = _commandCapacity * sizeof(uint32_t);
    materialDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    _materialIndexBuffer = AcquirePersistentBuffer(
        _materialIndexBuffer, materialDesc, "RendererBatch.MaterialIndex");

    BufferDesc objectDesc{};
    objectDesc.size = _instanceCapacity * sizeof(GPUObjectData);
    objectDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    _objectDataBuffer = AcquirePersistentBuffer(
        _objectDataBuffer, objectDesc, "RendererBatch.ObjectData");

    // GPU-written scatter target; only its capacity matters.
    BufferDesc instanceDesc{};
    instanceDesc.size = _instanceCapacity * sizeof(uint);
    instanceDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    _instanceBuffer = AcquirePersistentBuffer(
        _instanceBuffer, instanceDesc, "RendererBatch.Instance");

	// Live prefix in promotion order; the rest wait in _pendingDraws.
	for (const auto& [materialName, materialBatch] : _materialBatches)
	{
		for (const auto& [subMeshName, subMeshBatch] : materialBatch.SubMeshBatches)
		{
			auto* subMesh = subMeshBatch.SubMesh.TryGet();
			if (!subMesh)
				continue;

			auto* material = materialBatch.Material.TryGet();

			const bool ready = material && material->HasMaterialIndex()
				&& subMesh->HasAllocation() && subMeshBatch.SubMesh.IsResident();

			if (ready)
				AppendDraw(materialBatch.Material, subMeshBatch.SubMesh,
					subMeshBatch.Transforms);
			else
				_pendingDraws.emplace(subMeshName, PendingDraw{
					materialBatch.Material, subMeshBatch.SubMesh,
					subMeshBatch.Transforms });
		}
	}

    // One bulk fill per table for the rebuilt prefix.
    const auto& drawCommands = _indirectDrawBuffer.GetDrawCommands();
    const auto& materialIndices = 
        _indirectDrawBuffer.GetMaterialIndices();

    if (!drawCommands.empty())
    {
        _pendingTableFills.push_back({ _indirectCommandBuffer,
            ToBytes(drawCommands.data(), drawCommands.size()), 0 });
        _pendingTableFills.push_back({ _materialIndexBuffer,
            ToBytes(materialIndices.data(), materialIndices.size()), 0 });
        _pendingTableFills.push_back({ _objectDataBuffer,
            ToBytes(_objectData.data(), _objectData.size()), 0 });
    }

    if (anyTransform)
        _pendingTableFills.push_back({ _transformBuffer,
            ToBytes(transforms.data(), transforms.size()), 0 });

    _hasGpuBuffers = true;
}

void Core::RendererBatch::QueuePendingInit(FrameResources& frameResources)
{
    for (auto& fill : _pendingTableFills)
        frameResources.AddInitJob(make_unique<BufferUploadJob<uint8_t>>(
            _device, fill.Buffer.Get(), std::move(fill.Bytes), fill.Offset));

    _pendingTableFills.clear();
}

void Core::RendererBatch::Sync(Scene& scene)
{
    if (_dirty)
    {
        _dirty = false;

        _materialBatches.clear();
        _transforms.clear();

        InitializeFromScene(scene);
        RebuildGpuBuffers();
    }

    AppendPendingResident();
}
