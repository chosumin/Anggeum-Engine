#include "stdafx.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Material.h"
#include "Graphics/SubMesh.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/ResourceCache.h"
#include "Foundation/Entity.h"
#include "Foundation/Scene.h"
#include "Components/Mesh.h"
#include "Components/Transform.h"
#include "TransferJob.h"
#include "Graphics/TransferContext.h"

using namespace Core;

Core::RendererBatch::RendererBatch(Device& device)
    : _device(device)
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
    auto& cache = _device.GetResourceCache();
    if (current.IsValid())
    {
        cache.ResizeBuffer(current, desc, name);
        return current;
    }
    return cache.LoadBuffer(desc, name);
}

void Core::RendererBatch::RebuildGpuBuffers(TransferContext& transfer)
{
    // Bumped before the early-out below so an empty rebuild still counts: the draw
    // set changed either way, and Cullers have to notice.
    ++_revision;

    // Replacing a live buffer frees the old one immediately, so make sure no frame is
    // still reading it. The very first build happens before any frame is in flight.
    if (_hasGpuBuffers)
        vkDeviceWaitIdle(_device.GetDevice());

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
        _transformBatch.TransformBuffer = AcquirePersistentBuffer(
            _transformBatch.TransformBuffer, transformDesc, "RendererBatch.Transform");
    }

    // --- Instance + GPU-driven buffers ---
    _indirectDrawBuffer.Clear();
    _instanceCount = 0;
    for (auto& [materialName, materialBatch] : _materialBatches)
        for (auto& [subMeshName, subMeshBatch] : materialBatch.SubMeshBatches)
            _instanceCount += static_cast<uint>(subMeshBatch.Transforms.size());

    vector<uint> instanceData;
    instanceData.reserve(_instanceCount);

    vector<GPUObjectData> objectData;
    objectData.reserve(_instanceCount);

    uint32_t globalFirstInstance = 0;
    uint32_t drawCommandIndex = 0;

    for (auto& [materialName, materialBatch] : _materialBatches)
    {
        auto* material = materialBatch.Material.TryGet();
        if (!material || !material->HasMaterialIndex())
            continue;

        uint32_t materialIndex = material->GetMaterialIndex();

        for (auto& [subMeshName, subMeshBatch] : materialBatch.SubMeshBatches)
        {
            auto* subMesh = subMeshBatch.SubMesh.TryGet();
            if (!subMesh || !subMesh->HasAllocation())
                continue;

            const auto& allocation = subMesh->GetAllocation();
            uint32_t instanceCount = static_cast<uint32_t>(subMeshBatch.Transforms.size());

            _indirectDrawBuffer.AddDrawCommand(
                allocation,
                materialIndex,
                0,
                globalFirstInstance
            );

            for (size_t i = 0; i < subMeshBatch.Transforms.size(); ++i)
            {
                instanceData.push_back(subMeshBatch.Transforms[i]);

                GPUObjectData data{};
                data.boundingSphere = glm::vec4(
                    allocation.boundingSphereCenter,
                    allocation.boundingSphereRadius);
                data.transformIndex = subMeshBatch.Transforms[i];
                data.drawCommandIndex = drawCommandIndex;

                objectData.push_back(data);
            }

            globalFirstInstance += instanceCount;
            drawCommandIndex++;
        }
    }

    const auto& drawCommands = _indirectDrawBuffer.GetDrawCommands();
    const auto& materialIndices = _indirectDrawBuffer.GetMaterialIndices();

    // Nothing to draw: keep whatever (possibly empty) buffers exist; the draw paths
    // early-out on GetDrawCommandCount() == 0.
    if (drawCommands.empty())
    {
        _hasGpuBuffers = true;
        return;
    }

    BufferDesc indirectDesc{};
    indirectDesc.size = drawCommands.size() * sizeof(DrawIndexedIndirectCommand);
    indirectDesc.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    _indirectCommandBuffer = AcquirePersistentBuffer(
        _indirectCommandBuffer, indirectDesc, "RendererBatch.IndirectCommand");

    BufferDesc materialDesc{};
    materialDesc.size = materialIndices.size() * sizeof(uint32_t);
    materialDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    _materialIndexBuffer = AcquirePersistentBuffer(
        _materialIndexBuffer, materialDesc, "RendererBatch.MaterialIndex");

    BufferDesc objectDesc{};
    objectDesc.size = objectData.size() * sizeof(GPUObjectData);
    objectDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    _objectDataBuffer = AcquirePersistentBuffer(
        _objectDataBuffer, objectDesc, "RendererBatch.ObjectData");

    BufferDesc instanceDesc{};
    instanceDesc.size = instanceData.size() * sizeof(uint);
    instanceDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    _instanceBuffer = AcquirePersistentBuffer(
        _instanceBuffer, instanceDesc, "RendererBatch.Instance");

    // Enqueued rather than submitted here: the jobs stage their data on worker
    // threads and go out as one submit when the caller flushes the TransferContext,
    // instead of blocking this thread on a fence per buffer.
    transfer.Enqueue(new Core::VkBufferCopyJob<DrawIndexedIndirectCommand>(_device,
        _indirectCommandBuffer.Get(), vector<DrawIndexedIndirectCommand>(drawCommands), 0),
        "RendererBatch.IndirectCommand");

    transfer.Enqueue(new Core::VkBufferCopyJob<uint32_t>(_device,
        _materialIndexBuffer.Get(), vector<uint32_t>(materialIndices), 0),
        "RendererBatch.MaterialIndex");

    transfer.Enqueue(new Core::VkBufferCopyJob<GPUObjectData>(_device,
        _objectDataBuffer.Get(), move(objectData), 0),
        "RendererBatch.ObjectData");

    transfer.Enqueue(new Core::VkBufferCopyJob<uint>(_device,
        _instanceBuffer.Get(), move(instanceData), 0),
        "RendererBatch.Instance");

    // A transform buffer job only exists when there were transforms to upload.
    if (anyTransform)
    {
        transfer.Enqueue(new Core::VkBufferCopyJob<mat4>(_device,
            _transformBatch.TransformBuffer.Get(), move(transforms), 0),
            "RendererBatch.Transform");
    }

    _hasGpuBuffers = true;
}

void Core::RendererBatch::Sync(Scene& scene, TransferContext& transfer, VkExtent2D extents)
{
    if (!_dirty)
        return;

    _dirty = false;

    // Rebuild the draw set from scratch off the current scene.
    _extents = extents;

    _materialBatches.clear();
    _transforms.clear();

    InitializeFromScene(scene);
    RebuildGpuBuffers(transfer);
}
