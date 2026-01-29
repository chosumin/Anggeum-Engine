#pragma once

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

		MeshAllocation AllocateMesh(
			const vector<glm::vec3>& positions,
			const vector<glm::vec3>& normals,
			const vector<glm::vec2>& uvs,
			const vector<uint32_t>& indices);

		void FreeMesh(uint32_t meshID);
		void Defragment();

		// Global buffers Á¢±Ù
		VkBuffer GetPositionBuffer() const;
		VkBuffer GetNormalBuffer() const;
		VkBuffer GetUVBuffer() const;
		VkBuffer GetIndexBuffer() const;

		uint32_t GetTotalVertexCount() const { return _currentVertexOffset; }
		uint32_t GetTotalIndexCount() const { return _currentIndexOffset; }
		uint32_t GetAllocatedMeshCount() const { return static_cast<uint32_t>(_allocations.size()); }

		const MeshAllocation* GetAllocation(uint32_t meshID) const;

	private:
		glm::vec4 CalculateBoundingSphere(const vector<glm::vec3>& positions);
		void CopyBufferToGlobal(Buffer* stagingBuffer, Buffer* dstBuffer, VkDeviceSize offset, VkDeviceSize size);

	private:
		Device& _device;

		shared_ptr<Buffer> _globalPositionBuffer;
		shared_ptr<Buffer> _globalNormalBuffer;
		shared_ptr<Buffer> _globalUVBuffer;
		shared_ptr<Buffer> _globalIndexBuffer;

		// Allocation tracking
		uint32_t _maxVertices = 10'000'000;
		uint32_t _maxIndices = 30'000'000;
		uint32_t _currentVertexOffset = 0;
		uint32_t _currentIndexOffset = 0;

		unordered_map<uint32_t, MeshAllocation> _allocations;
		vector<uint32_t> _freeList;
		uint32_t _nextMeshID = 0;
	};
}