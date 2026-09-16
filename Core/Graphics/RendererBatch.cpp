#include "stdafx.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Material.h"
#include "Graphics/SubMesh.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/SyncContext.h"
#include "Foundation/Entity.h"
#include "Foundation/Scene.h"
#include "Components/Mesh.h"
#include "Components/Transform.h"

#include "Graphics/BufferUpload.h"
#include "Graphics/FrameResources.h"

using namespace Core;

namespace
{
    template<typename T>
    vector<uint8_t> ToBytes(const T* data, size_t count)
    {
        vector<uint8_t> bytes(count * sizeof(T));
        memcpy(bytes.data(), data, bytes.size());
        return bytes;
    }

    // Instance readiness: the tables carry material indices per instance.
    bool HasMaterialIndex(const Handle<Material>& material)
    {
        auto* ptr = material.TryGet();
        return ptr && ptr->HasMaterialIndex();
    }
}

Core::RendererBatch::RendererBatch(Device& device, ResourceManager& resourceManager,
    SyncContext& syncContext)
    : _device(device), _resourceManager(resourceManager), _sync(syncContext)
{
}

Core::RendererBatch::~RendererBatch() = default;

void Core::RendererBatch::CollectDrawBatches(Scene& scene,
    unordered_map<string, DrawBatch>& drawBatches,
    unordered_map<uint, glm::mat4>& entityTransforms) const
{
    for (auto* mesh : scene.GetComponents<Mesh>())
    {
        uint entityId = mesh->GetEntity().GetId();
        glm::mat4 world = mesh->GetEntity().GetTransform().GetMatrix();

        auto& materials = mesh->GetMaterials();
        auto& subMeshes = mesh->GetSubMeshes();

        for (size_t i = 0; i < materials.size() && i < subMeshes.size(); ++i)
        {
            auto* material = materials[i].TryGet();
            auto* subMesh = subMeshes[i].TryGet();
            if (!material || !subMesh)
                continue;

            // Skip non-geometry passes
            auto shaderHandle = material->GetShaderHandle();
            if (!shaderHandle.IsValid() || shaderHandle.Get().GetPass() != "Geometry")
                continue;

            entityTransforms[entityId] = world;

            DrawBatch& batch = drawBatches[subMesh->GetName()];
            batch.SubMesh = subMeshes[i];
            batch.Instances.push_back({ entityId, materials[i] });
        }
    }
}

void Core::RendererBatch::SyncDrawBatches(Scene& scene)
{
    unordered_map<string, DrawBatch> incoming;
    unordered_map<uint, glm::mat4> entityTransforms;
    CollectDrawBatches(scene, incoming, entityTransforms);

    for (auto it = _drawBatches.begin(); it != _drawBatches.end();)
    {
        auto match = incoming.find(it->first);
        if (match != incoming.end() && match->second.Instances == it->second.Instances)
        {
			// Didn't change: keep the slot and range.
            incoming.erase(match);
            ++it;
            continue;
        }

		// Left or changed: free the slot and range, then remove the batch.
        ReleaseDrawBatch(it->second);
        it = _drawBatches.erase(it);
    }

	// New or changed draws: add them to the table and place them if resident.
    for (auto& [key, batch] : incoming)
        _drawBatches.emplace(key, std::move(batch));

    SyncTransforms(std::move(entityTransforms));
}

void Core::RendererBatch::SyncTransforms(unordered_map<uint, glm::mat4>&& entityTransforms)
{
    uint32_t needed = 0;
    for (const auto& [entityId, matrix] : entityTransforms)
        needed = std::max(needed, static_cast<uint32_t>(entityId) + 1);

    if (needed > _transformCapacity)
    {
        // Grown: the buffer is recreated, so the whole table is refilled.
        _transformCapacity = std::max(needed, _transformCapacity * 2);

        BufferDesc desc{ _transformCapacity * sizeof(glm::mat4),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT };
        _transformBuffer = AcquirePersistentBuffer(_transformBuffer, desc,
            "RendererBatch.Transform");

        vector<glm::mat4> table(_transformCapacity, glm::mat4(1.0f));
        for (const auto& [entityId, matrix] : entityTransforms)
            table[entityId] = matrix;

        _pendingTableFills.push_back({ _transformBuffer,
            ToBytes(table.data(), table.size()), 0 });
    }
    else
    {
        for (const auto& [entityId, matrix] : entityTransforms)
        {
            auto old = _entityTransforms.find(entityId);
            if (old != _entityTransforms.end() && old->second == matrix)
                continue;

            _pendingTableFills.push_back({ _transformBuffer,
                ToBytes(&matrix, 1), entityId * sizeof(glm::mat4) });
        }
    }

    _entityTransforms = std::move(entityTransforms);
}

void Core::RendererBatch::PlacePendingDrawBatches()
{
    for (auto& [key, batch] : _drawBatches)
    {
        if (batch.CmdSlot != NO_SLOT)
            continue;

        auto* subMesh = batch.SubMesh.TryGet();
        if (!subMesh || !subMesh->HasAllocation() || !batch.SubMesh.IsResident())
            continue;
        if (!std::all_of(batch.Instances.begin(), batch.Instances.end(),
            [](const DrawInstance& instance) { return HasMaterialIndex(instance.Material); }))
            continue;

        if (!TryPlaceDrawBatch(batch))
        {
            GrowAndRepack();
            bool placed = TryPlaceDrawBatch(batch);
            assert(placed && "capacity grew to fit every draw");
        }
    }
}

bool Core::RendererBatch::TryPlaceDrawBatch(DrawBatch& batch)
{
    ReclaimRetired();

    uint32_t slot = 0;
    if (!TryAllocateCommandSlot(slot))
        return false;

    uint32_t firstInstance = 0;
    if (!TryAllocateInstanceRange(static_cast<uint32_t>(batch.Instances.size()), firstInstance))
    {
        _freeCommandSlots.push_back(slot);
        return false;
    }

    batch.CmdSlot = slot;
    batch.FirstInstance = firstInstance;
    QueueDrawBatchFills(batch);

    return true;
}

DrawIndexedIndirectCommand Core::RendererBatch::BuildCommand(const DrawBatch& batch) const
{
    const auto& allocation = batch.SubMesh.Get().GetAllocation();

    DrawIndexedIndirectCommand cmd{};
    cmd.indexCount = allocation.indexCount;
    cmd.instanceCount = 0;
    cmd.firstIndex = allocation.indexOffset;
    cmd.vertexOffset = static_cast<int32_t>(allocation.vertexOffset);
    cmd.firstInstance = batch.FirstInstance;
    return cmd;
}

vector<GPUInstanceData> Core::RendererBatch::BuildInstances(const DrawBatch& batch) const
{
    const auto& allocation = batch.SubMesh.Get().GetAllocation();

    vector<GPUInstanceData> instances(batch.Instances.size());
    for (size_t i = 0; i < instances.size(); ++i)
    {
        const DrawInstance& instance = batch.Instances[i];
        GPUInstanceData& data = instances[i];
        memcpy(data.aabbMin, &allocation.boundsMin, sizeof(data.aabbMin));
        memcpy(data.aabbMax, &allocation.boundsMax, sizeof(data.aabbMax));
        data.transformIndex = instance.EntityId;
        data.drawCommandIndex = batch.CmdSlot;
        data.materialIndex = instance.Material.Get().GetMaterialIndex();
    }
    return instances;
}

void Core::RendererBatch::QueueDrawBatchFills(const DrawBatch& batch)
{
    const DrawIndexedIndirectCommand cmd = BuildCommand(batch);
    _pendingTableFills.push_back({ _indirectCommandBuffer,
        ToBytes(&cmd, 1), batch.CmdSlot * sizeof(DrawIndexedIndirectCommand) });

    const vector<GPUInstanceData> instances = BuildInstances(batch);
    _pendingTableFills.push_back({ _instanceDataBuffer,
        ToBytes(instances.data(), instances.size()),
        batch.FirstInstance * sizeof(GPUInstanceData) });
}

void Core::RendererBatch::ReleaseDrawBatch(DrawBatch& batch)
{
    if (batch.CmdSlot == NO_SLOT)
        return;

    const uint32_t count = static_cast<uint32_t>(batch.Instances.size());

    // Rebuilt byte-identical with only the dead dword changed, so an in-flight
    // reader sees the old entry or the dead one; every consumer keys off it.
    // The slot and range are handed out again only after those readers retire.
    vector<GPUInstanceData> instances = BuildInstances(batch);
    for (GPUInstanceData& data : instances)
        data.drawCommandIndex = DEAD_DRAW;

    _pendingTableFills.push_back({ _instanceDataBuffer,
        ToBytes(instances.data(), instances.size()),
        batch.FirstInstance * sizeof(GPUInstanceData) });

    const u64 stamp = _sync.GetCurrentValue(QueueType::Graphics);
    _retiredCommandSlots.emplace_back(batch.CmdSlot, stamp);
    _retiredInstanceRanges.emplace_back(InstanceRange{ batch.FirstInstance, count }, stamp);

    batch.CmdSlot = NO_SLOT;
}

void Core::RendererBatch::ReclaimRetired()
{
    const u64 completed = _sync.GetCompletedValue(QueueType::Graphics);

    while (!_retiredCommandSlots.empty() && completed >= _retiredCommandSlots.front().second)
    {
        _freeCommandSlots.push_back(_retiredCommandSlots.front().first);
        _retiredCommandSlots.pop_front();
    }
    while (!_retiredInstanceRanges.empty() && completed >= _retiredInstanceRanges.front().second)
    {
        FreeInstanceRange(_retiredInstanceRanges.front().first);
        _retiredInstanceRanges.pop_front();
    }
}

bool Core::RendererBatch::TryAllocateCommandSlot(uint32_t& outSlot)
{
    if (!_freeCommandSlots.empty())
    {
        outSlot = _freeCommandSlots.back();
        _freeCommandSlots.pop_back();
        return true;
    }

    if (_commandSlotEnd < _commandCapacity)
    {
        outSlot = _commandSlotEnd++;
        return true;
    }

    return false;
}

bool Core::RendererBatch::TryAllocateInstanceRange(uint32_t count, uint32_t& outOffset)
{
    for (auto it = _freeInstanceRanges.begin(); it != _freeInstanceRanges.end(); ++it)
    {
        if (it->count < count)
            continue;

        outOffset = it->offset;
        it->offset += count;
        it->count -= count;
        if (it->count == 0)
            _freeInstanceRanges.erase(it);
        return true;
    }

    if (_instanceSlotEnd + count <= _instanceCapacity)
    {
        outOffset = _instanceSlotEnd;
        _instanceSlotEnd += count;
        return true;
    }

    return false;
}

void Core::RendererBatch::FreeInstanceRange(InstanceRange range)
{
    if (range.count == 0)
        return;

    auto next = std::lower_bound(_freeInstanceRanges.begin(), _freeInstanceRanges.end(), range,
        [](const InstanceRange& a, const InstanceRange& b) { return a.offset < b.offset; });
    auto it = _freeInstanceRanges.insert(next, range);

    // Coalesce with the neighbours on either side.
    if (it != _freeInstanceRanges.begin())
    {
        auto prev = std::prev(it);
        if (prev->offset + prev->count == it->offset)
        {
            prev->count += it->count;
            _freeInstanceRanges.erase(it);
            it = prev;
        }
    }
    auto after = std::next(it);
    if (after != _freeInstanceRanges.end() && it->offset + it->count == after->offset)
    {
        it->count += after->count;
        _freeInstanceRanges.erase(after);
    }
}

void Core::RendererBatch::GrowAndRepack()
{
    uint32_t commandsNeeded = 0;
    uint32_t instancesNeeded = 0;
    for (const auto& [key, batch] : _drawBatches)
    {
        ++commandsNeeded;
        instancesNeeded += static_cast<uint32_t>(batch.Instances.size());
    }

    _commandCapacity = std::max(commandsNeeded, _commandCapacity * 2);
    _instanceCapacity = std::max(instancesNeeded, _instanceCapacity * 2);

    // Recreated buffers start empty, so fills already queued for them are void.
    _pendingTableFills.erase(std::remove_if(_pendingTableFills.begin(), _pendingTableFills.end(),
        [this](const TableFill& fill)
        {
            return fill.Buffer == _indirectCommandBuffer || fill.Buffer == _instanceDataBuffer;
        }), _pendingTableFills.end());

    BufferDesc indirectDesc{ _commandCapacity * sizeof(DrawIndexedIndirectCommand),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT };
    _indirectCommandBuffer = AcquirePersistentBuffer(
        _indirectCommandBuffer, indirectDesc, "RendererBatch.IndirectCommand");

    BufferDesc objectDesc{ _instanceCapacity * sizeof(GPUInstanceData),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT };
    _instanceDataBuffer = AcquirePersistentBuffer(
        _instanceDataBuffer, objectDesc, "RendererBatch.InstanceData");

    // The old tables retire whole with their buffers: nothing to hand back.
    _freeCommandSlots.clear();
    _retiredCommandSlots.clear();
    _freeInstanceRanges.clear();
    _retiredInstanceRanges.clear();
    _commandSlotEnd = 0;
    _instanceSlotEnd = 0;

    // Re-place every resident draw densely.
    for (auto& [key, batch] : _drawBatches)
    {
        if (batch.CmdSlot == NO_SLOT)
            continue;

        bool slotted = TryAllocateCommandSlot(batch.CmdSlot);
        bool ranged = TryAllocateInstanceRange(
            static_cast<uint32_t>(batch.Instances.size()), batch.FirstInstance);
        assert(slotted && ranged);
    }

    if (_commandSlotEnd == 0)
        return;

    // Fill the prefix in bulk.
    vector<DrawIndexedIndirectCommand> commands(_commandSlotEnd);
    vector<GPUInstanceData> instances(_instanceSlotEnd);
    for (const auto& [key, batch] : _drawBatches)
    {
        if (batch.CmdSlot == NO_SLOT)
            continue;

        commands[batch.CmdSlot] = BuildCommand(batch);
        const vector<GPUInstanceData> batchInstances = BuildInstances(batch);
        std::copy(batchInstances.begin(), batchInstances.end(),
            instances.begin() + batch.FirstInstance);
    }

    _pendingTableFills.push_back({ _indirectCommandBuffer,
        ToBytes(commands.data(), commands.size()), 0 });
    _pendingTableFills.push_back({ _instanceDataBuffer,
        ToBytes(instances.data(), instances.size()), 0 });
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
        SyncDrawBatches(scene);
    }

    PlacePendingDrawBatches();
}
