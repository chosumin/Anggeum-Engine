#pragma once
#include "ResourcePool.h"
#include "GeometryUpload.h"
#include "BufferUpload.h"

namespace Core
{
	struct MeshAllocation
	{
		uint32_t vertexOffset;
		uint32_t vertexCount;
		uint32_t indexOffset;
		uint32_t indexCount;
		uint32_t meshID;
		
		glm::vec3 boundingSphereCenter;
		float boundingSphereRadius;
	};

	class Buffer;

	// Destination of a reserved region in the shared storage: which buffer to copy
	// into and at what byte offset.
	struct MeshBufferRegion
	{
		Handle<Buffer> destination;
		VkDeviceSize offset;
	};

	class MeshBufferManager
	{
	public:
		MeshBufferManager(Device& device);
		~MeshBufferManager();

		// Where loaders drop raw geometry. A non-empty queue IS this manager's dirty
		// state; Sync() drains it.
		GeometryUploadQueue& GetUploadQueue() { return _uploadQueue; }

		// Flush pending geometry into the global buffers: reserve spans and record each
		// SubMesh's MeshAllocation. Returns the copies to perform (one BufferUpload per
		// source mesh) — this manager does allocation only; the transfer jobs are
		// issued by RenderScene::Sync. Empty when nothing was queued.
		vector<BufferUpload> Sync();

		void FreeMesh(uint32_t meshID);
		void Defragment();

		// High-water marks of the shared vertex/index storage. With the free-span
		// suballocator these are the tail, not the sum of live allocations (freed
		// spans below the tail are reused rather than reclaimed from the count).
		uint32_t GetTotalVertexCount() const { return _vertexTail; }
		uint32_t GetTotalIndexCount() const { return _indexTail; }
		uint32_t GetAllocatedMeshCount() const { return static_cast<uint32_t>(_allocations.size()); }

		const MeshAllocation* GetAllocation(uint32_t meshID) const;

		// Reserve space for a vertex attribute / index buffer and return where to copy
		// it (buffer + byte offset). `data` is read (count/bounds) but not
		// consumed. Call Build() after a submesh's attributes+index are reserved.
		MeshBufferRegion Allocate(const string& name, uint32_t stride, const vector<uint8_t>& data);
		MeshBufferRegion Allocate(VkIndexType indexType, const vector<uint8_t>& indexData);
		MeshAllocation Build();

		// Buffers are pool-owned (handle pattern) so the global vertex/index storage
		// can be resized/relocated in place later via ResourcePool::Replace, while
		// holders keep their handles. Resolve with handle.Get().
		vector<Handle<Buffer>> GetVertexBuffers(vector<string> names) const;
		Handle<Buffer> GetIndexBuffer() const { return _indexBufferHandle; }

		VkIndexType GetIndexType() const { return _indexType; }

		// Scene-wide bounds accumulated during mesh loading
		const glm::vec3& GetSceneBoundsMin() const { return _sceneBoundsMin; }
		const glm::vec3& GetSceneBoundsMax() const { return _sceneBoundsMax; }

	private:
		// A free region of the shared storage, measured in elements (vertices or
		// indices), not bytes. Kept sorted by offset so neighbours can coalesce.
		struct FreeSpan
		{
			uint32_t offset;
			uint32_t size;
		};

		glm::vec4 CalculateBoundingSphere(const vector<glm::vec3>& positions);
		Handle<Buffer> InsertBufferSpace(VkIndexType indexType);

		// Carve `count` elements out of the free-span list (first-fit), falling back
		// to extending the tail. `maxCount` bounds the tail against the buffer size.
		static uint32_t AllocateSpan(vector<FreeSpan>& freeSpans, uint32_t& tail,
			uint32_t count, uint32_t maxCount, const char* what);
		// Return a span to the free list, coalescing with neighbours; shrinks the
		// tail when the freed region sits at the very end.
		static void ReleaseSpan(vector<FreeSpan>& freeSpans, uint32_t& tail,
			uint32_t offset, uint32_t count);
	private:
		Device& _device;

		//Key: Attribute name, Value: handle
		unordered_map<string, Handle<Buffer>> _vertexBufferHandles;

		VkIndexType _indexType;
		Handle<Buffer> _indexBufferHandle;

		// Allocation tracking. Geometry is variable-sized (each submesh differs), so
		// load/unload fragments the storage; a size-aware free-span list reuses freed
		// regions. Fixed-size meshlet pooling would replace this later.
		uint32_t _maxVertices = 10'000'000;
		uint32_t _maxIndices = 30'000'000;
		uint32_t _vertexTail = 0;
		uint32_t _indexTail = 0;
		vector<FreeSpan> _vertexFreeSpans;
		vector<FreeSpan> _indexFreeSpans;

		// The span reserved for the submesh currently being assembled (one span reused
		// across all its vertex attributes, committed on Build).
		uint32_t _pendingVertexOffset = 0;
		uint32_t _pendingVertexCount = 0;
		bool _hasPendingVertexSpan = false;
		uint32_t _pendingIndexOffset = 0;
		uint32_t _pendingIndexCount = 0;
		vec4 _tempBoundingSphere;

		unordered_map<uint32_t, MeshAllocation> _allocations;
		vector<uint32_t> _freeList;
		uint32_t _nextMeshID = 0;

		// Raw geometry handed over by loaders, drained by Sync().
		GeometryUploadQueue _uploadQueue;

		// Scene-wide local-space bounds (accumulated across all POSITION allocations)
		glm::vec3 _sceneBoundsMin{FLT_MAX};
		glm::vec3 _sceneBoundsMax{-FLT_MAX};
	};
}