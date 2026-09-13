#pragma once
#include "BufferObjects.h"
#include "ResourceHandle.h"
#include "ResourcePool.h"

namespace Core
{
	class Material;
	class SubMesh;
	class Buffer;
	class Scene;
	class ResourceManager;
	class FrameResources;
	class SyncContext;

	// RendererBatch: the application-wide GPU-driven draw set, shared by every
	// frame-in-flight (culling reads it; mutable outputs are per-frame graph
	// buffers). Slot tables: a draw owns a command slot and an instance range
	// for its lifetime, membership changes patch only the affected entries,
	// freed slots return through a frame-stamped retire, and the buffers are
	// recreated only when the capacity runs out.
	class RendererBatch
	{
	public:
		static constexpr const char* SB_INSTANCE_IDS = "RendererBatch.Instance";

		static constexpr uint32_t DEAD_DRAW = 0xFFFFFFFFu;

		RendererBatch(Device& device, ResourceManager& resourceManager,
			SyncContext& syncContext);
		~RendererBatch();

		void MarkDirty() { _dirty = true; }

		// Membership diff when dirty, then places newly-resident geometry.
		void Sync(Scene& scene);

		void QueuePendingInit(FrameResources& frameResources);

		Buffer& GetObjectDataBuffer() const { return _objectDataBuffer.Get(); }
		Buffer& GetIndirectCommandBuffer() const { return _indirectCommandBuffer.Get(); }
		Buffer& GetMaterialIndexBuffer() const { return _materialIndexBuffer.Get(); }
		Buffer& GetInstanceBuffer() const { return _instanceBuffer.Get(); }
		Handle<Buffer> GetInstanceBufferHandle() const { return _instanceBuffer; }
		Buffer& GetTransformBuffer() const { return _transformBuffer.Get(); }

		// One past the last slot ever handed out: the culls dispatch over
		// [0, end), dead entries included.
		uint32_t GetDrawCommandCount() const { return _commandSlotEnd; }
		uint32_t GetInstanceCount() const { return _instanceSlotEnd; }

	private:
		// One (material, submesh) draw: its membership and, once resident,
		// the slot and instance range it owns.
		struct DrawRecord
		{
			Handle<Material> Material;
			Handle<SubMesh> SubMesh;
			vector<uint> Entities;
			uint32_t CmdSlot = DEAD_DRAW;
			uint32_t FirstInstance = 0;
		};

		struct InstanceRange
		{
			uint32_t offset;
			uint32_t count;
		};

		struct TableFill
		{
			Handle<Buffer> Buffer;
			vector<uint8_t> Bytes;
			VkDeviceSize Offset = 0;
		};

		void CollectDrawRecords(Scene& scene, unordered_map<string, DrawRecord>& drawRecords,
			unordered_map<uint, glm::mat4>& entityTransforms) const;
		void SyncDrawRecords(Scene& scene);
		void SyncTransforms(unordered_map<uint, glm::mat4>&& entityTransforms);

		void PlacePendingDrawRecords();
		bool TryPlaceDrawRecord(DrawRecord& record);
		void ReleaseDrawRecord(DrawRecord& record);
		// Mirrors only; the GPU patch for one record is queued separately
		// (GrowAndRepack refills the whole prefix instead).
		void WriteDrawRecord(const DrawRecord& record);
		void QueueDrawRecordFills(const DrawRecord& record);

		void ReclaimRetired();
		bool TryAllocateCommandSlot(uint32_t& outSlot);
		bool TryAllocateInstanceRange(uint32_t count, uint32_t& outOffset);
		void FreeInstanceRange(InstanceRange range);

		// Recreates the tables at a larger capacity and re-places every
		// resident draw densely.
		void GrowAndRepack();

		// Create the buffer on first use; replace only when the size changed.
		Handle<Buffer> AcquirePersistentBuffer(Handle<Buffer> current,
			const struct BufferDesc& desc, const string& name);

	private:
		Device& _device;
		ResourceManager& _resourceManager;
		SyncContext& _sync;

		vector<TableFill> _pendingTableFills;

		unordered_map<string, DrawRecord> _drawRecords;
		unordered_map<uint, glm::mat4> _entityTransforms;

		// CPU mirrors, capacity-sized; the GPU tables are patched from them.
		vector<DrawIndexedIndirectCommand> _commands;
		vector<uint32_t> _materialIndices;
		vector<GPUObjectData> _objectData;

		Handle<Buffer> _indirectCommandBuffer;
		Handle<Buffer> _materialIndexBuffer;
		Handle<Buffer> _objectDataBuffer;
		Handle<Buffer> _instanceBuffer;   // GPU-written scatter target
		Handle<Buffer> _transformBuffer;

		uint32_t _commandCapacity = 0;
		uint32_t _commandSlotEnd = 0;
		vector<uint32_t> _freeCommandSlots;
		deque<pair<uint32_t, u64>> _retiredCommandSlots;

		uint32_t _instanceCapacity = 0;
		uint32_t _instanceSlotEnd = 0;
		vector<InstanceRange> _freeInstanceRanges;                     // sorted by offset
		deque<pair<InstanceRange, u64>> _retiredInstanceRanges;

		uint32_t _transformCapacity = 0;

		bool _dirty = false;   // scene membership changed since the last sync
	};
}
