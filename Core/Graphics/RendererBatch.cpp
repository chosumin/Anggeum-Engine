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

    string DrawRecordKey(const Material& material, const SubMesh& subMesh)
    {
        return material.GetName() + "|" + subMesh.GetName();
    }
}

Core::RendererBatch::RendererBatch(Device& device, ResourceManager& resourceManager,
    SyncContext& syncContext)
    : _device(device), _resourceManager(resourceManager), _sync(syncContext)
{
}

Core::RendererBatch::~RendererBatch() = default;

void Core::RendererBatch::CollectDrawRecords(Scene& scene,
    unordered_map<string, DrawRecord>& drawRecords,
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

            DrawRecord& record = drawRecords[DrawRecordKey(*material, *subMesh)];
            record.Material = materials[i];
            record.SubMesh = subMeshes[i];
            record.Entities.push_back(entityId);
        }
    }
}

void Core::RendererBatch::SyncDrawRecords(Scene& scene)
{
    unordered_map<string, DrawRecord> incoming;
    unordered_map<uint, glm::mat4> entityTransforms;
    CollectDrawRecords(scene, incoming, entityTransforms);

    for (auto it = _drawRecords.begin(); it != _drawRecords.end();)
    {
        auto match = incoming.find(it->first);
        if (match != incoming.end() && match->second.Entities == it->second.Entities)
        {
			// Didn't change: keep the slot and range.
            incoming.erase(match);
            ++it;
            continue;
        }

		// Left or changed: free the slot and range, then remove the record.
        ReleaseDrawRecord(it->second);
        it = _drawRecords.erase(it);
    }

	// New or changed draws: add them to the table and place them if resident.
    for (auto& [key, record] : incoming)
        _drawRecords.emplace(key, std::move(record));

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

void Core::RendererBatch::PlacePendingDrawRecords()
{
    for (auto& [key, record] : _drawRecords)
    {
        if (record.CmdSlot != NO_SLOT)
            continue;

        auto* subMesh = record.SubMesh.TryGet();
        auto* material = record.Material.TryGet();
        if (!subMesh || !material || !material->HasMaterialIndex()
            || !subMesh->HasAllocation() || !record.SubMesh.IsResident())
            continue;

        if (!TryPlaceDrawRecord(record))
        {
            GrowAndRepack();
            bool placed = TryPlaceDrawRecord(record);
            assert(placed && "capacity grew to fit every draw");
        }
    }
}

bool Core::RendererBatch::TryPlaceDrawRecord(DrawRecord& record)
{
    ReclaimRetired();

    uint32_t slot = 0;
    if (!TryAllocateCommandSlot(slot))
        return false;

    uint32_t firstInstance = 0;
    if (!TryAllocateInstanceRange(static_cast<uint32_t>(record.Entities.size()), firstInstance))
    {
        _freeCommandSlots.push_back(slot);
        return false;
    }

    record.CmdSlot = slot;
    record.FirstInstance = firstInstance;
    WriteDrawRecord(record);
    QueueDrawRecordFills(record);
    return true;
}

void Core::RendererBatch::WriteDrawRecord(const DrawRecord& record)
{
    const auto& allocation = record.SubMesh.Get().GetAllocation();
    const uint32_t count = static_cast<uint32_t>(record.Entities.size());

    DrawIndexedIndirectCommand& cmd = _commands[record.CmdSlot];
    cmd.indexCount = allocation.indexCount;
    cmd.instanceCount = 0;
    cmd.firstIndex = allocation.indexOffset;
    cmd.vertexOffset = static_cast<int32_t>(allocation.vertexOffset);
    cmd.firstInstance = record.FirstInstance;

    _materialIndices[record.CmdSlot] = record.Material.Get().GetMaterialIndex();

    for (uint32_t i = 0; i < count; ++i)
    {
        GPUObjectData& data = _objectData[record.FirstInstance + i];
        data.boundingSphere = glm::vec4(
            allocation.boundingSphereCenter, allocation.boundingSphereRadius);
        data.transformIndex = record.Entities[i];
        data.drawCommandIndex = record.CmdSlot;
    }
}

void Core::RendererBatch::QueueDrawRecordFills(const DrawRecord& record)
{
    const uint32_t count = static_cast<uint32_t>(record.Entities.size());

    _pendingTableFills.push_back({ _indirectCommandBuffer,
        ToBytes(&_commands[record.CmdSlot], 1),
        record.CmdSlot * sizeof(DrawIndexedIndirectCommand) });
    _pendingTableFills.push_back({ _materialIndexBuffer,
        ToBytes(&_materialIndices[record.CmdSlot], 1), record.CmdSlot * sizeof(uint32_t) });
    _pendingTableFills.push_back({ _objectDataBuffer,
        ToBytes(&_objectData[record.FirstInstance], count),
        record.FirstInstance * sizeof(GPUObjectData) });
}

void Core::RendererBatch::ReleaseDrawRecord(DrawRecord& record)
{
    if (record.CmdSlot == NO_SLOT)
        return;

    const uint32_t count = static_cast<uint32_t>(record.Entities.size());

    // Dead-marked in place (one dword per entry, so an in-flight reader sees
    // the old entry or the dead one, never a torn mix); every consumer keys
    // off this field. The slot and range are handed out again only after
    // those readers retire.
    for (uint32_t i = 0; i < count; ++i)
        _objectData[record.FirstInstance + i].drawCommandIndex = DEAD_DRAW;

    _pendingTableFills.push_back({ _objectDataBuffer,
        ToBytes(&_objectData[record.FirstInstance], count),
        record.FirstInstance * sizeof(GPUObjectData) });

    const u64 stamp = _sync.GetCurrentValue(QueueType::Graphics);
    _retiredCommandSlots.emplace_back(record.CmdSlot, stamp);
    _retiredInstanceRanges.emplace_back(InstanceRange{ record.FirstInstance, count }, stamp);

    record.CmdSlot = NO_SLOT;
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
    for (const auto& [key, record] : _drawRecords)
    {
        ++commandsNeeded;
        instancesNeeded += static_cast<uint32_t>(record.Entities.size());
    }

    _commandCapacity = std::max(commandsNeeded, _commandCapacity * 2);
    _instanceCapacity = std::max(instancesNeeded, _instanceCapacity * 2);

    // Recreated buffers start empty, so fills already queued for them are void.
    _pendingTableFills.erase(std::remove_if(_pendingTableFills.begin(), _pendingTableFills.end(),
        [this](const TableFill& fill)
        {
            return fill.Buffer == _indirectCommandBuffer || fill.Buffer == _materialIndexBuffer
                || fill.Buffer == _objectDataBuffer;
        }), _pendingTableFills.end());

    BufferDesc indirectDesc{ _commandCapacity * sizeof(DrawIndexedIndirectCommand),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT };
    _indirectCommandBuffer = AcquirePersistentBuffer(
        _indirectCommandBuffer, indirectDesc, "RendererBatch.IndirectCommand");

    BufferDesc materialDesc{ _commandCapacity * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT };
    _materialIndexBuffer = AcquirePersistentBuffer(
        _materialIndexBuffer, materialDesc, "RendererBatch.MaterialIndex");

    BufferDesc objectDesc{ _instanceCapacity * sizeof(GPUObjectData),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT };
    _objectDataBuffer = AcquirePersistentBuffer(
        _objectDataBuffer, objectDesc, "RendererBatch.ObjectData");

    _commands.assign(_commandCapacity, DrawIndexedIndirectCommand{});
    _materialIndices.assign(_commandCapacity, 0);
    GPUObjectData dead{};
    dead.drawCommandIndex = DEAD_DRAW;
    _objectData.assign(_instanceCapacity, dead);

    // The old tables retire whole with their buffers: nothing to hand back.
    _freeCommandSlots.clear();
    _retiredCommandSlots.clear();
    _freeInstanceRanges.clear();
    _retiredInstanceRanges.clear();
    _commandSlotEnd = 0;
    _instanceSlotEnd = 0;

    // Re-place every resident draw densely, then fill the prefix in bulk.
    for (auto& [key, record] : _drawRecords)
    {
        if (record.CmdSlot == NO_SLOT)
            continue;

        bool slotted = TryAllocateCommandSlot(record.CmdSlot);
        bool ranged = TryAllocateInstanceRange(
            static_cast<uint32_t>(record.Entities.size()), record.FirstInstance);
        assert(slotted && ranged);
        WriteDrawRecord(record);
    }

    if (_commandSlotEnd > 0)
    {
        _pendingTableFills.push_back({ _indirectCommandBuffer,
            ToBytes(_commands.data(), _commandSlotEnd), 0 });
        _pendingTableFills.push_back({ _materialIndexBuffer,
            ToBytes(_materialIndices.data(), _commandSlotEnd), 0 });
        _pendingTableFills.push_back({ _objectDataBuffer,
            ToBytes(_objectData.data(), _instanceSlotEnd), 0 });
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
        SyncDrawRecords(scene);
    }

    PlacePendingDrawRecords();
}
