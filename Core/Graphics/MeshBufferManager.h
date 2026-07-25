#pragma once
#include "ResourcePool.h"

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

	class TransferContext;
	class Buffer;
	class MeshBufferManager
	{
	public:
		MeshBufferManager(Device& device);
		~MeshBufferManager();

		void FreeMesh(uint32_t meshID);
		void Defragment();

		uint32_t GetTotalVertexCount() const { return _currentVertexOffset; }
		uint32_t GetTotalIndexCount() const { return _currentIndexOffset; }
		uint32_t GetAllocatedMeshCount() const { return static_cast<uint32_t>(_allocations.size()); }

		const MeshAllocation* GetAllocation(uint32_t meshID) const;

		void Allocate(TransferContext& transferContext, const string& name, uint32_t stride, vector<uint8_t>&& data, string subMeshName);
		void Allocate(TransferContext& transferContext, VkIndexType indexType, vector<uint8_t>&& indexData, string subMeshName);
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
		glm::vec4 CalculateBoundingSphere(const vector<glm::vec3>& positions);
		Handle<Buffer> InsertBufferSpace(VkIndexType indexType);
	private:
		Device& _device;

		ResourcePool<Buffer> _bufferPool;

		//Key: Attribute name, Value: handle into _bufferPool
		unordered_map<string, Handle<Buffer>> _vertexBufferHandles;

		VkIndexType _indexType;
		Handle<Buffer> _indexBufferHandle;

		// Allocation tracking
		uint32_t _maxVertices = 10'000'000;
		uint32_t _maxIndices = 30'000'000;
		uint32_t _currentVertexOffset = 0;
		uint32_t _currentIndexOffset = 0;

		uint32_t _tempVertexOffset = 0;
		uint32_t _tempIndexCount = 0;
		vec4 _tempBoundingSphere;

		unordered_map<uint32_t, MeshAllocation> _allocations;
		vector<uint32_t> _freeList;
		uint32_t _nextMeshID = 0;

		// Scene-wide local-space bounds (accumulated across all POSITION allocations)
		glm::vec3 _sceneBoundsMin{FLT_MAX};
		glm::vec3 _sceneBoundsMax{-FLT_MAX};
	};
}