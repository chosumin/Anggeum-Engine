#pragma once
#include "Graphics/MeshBufferManager.h"

namespace Core
{
	class Buffer;
	class CommandBuffer;
	class SubMesh
	{
	public:
		SubMesh(Device& device, string name);
		~SubMesh();

		uint32_t GetIndexCount() const { return _indexCount; }
		void SetIndexCount(uint32_t count) { _indexCount = count; }

		vector<Buffer*> GetVertexBuffers(vector<string> names) const;
		Buffer& GetIndexBuffer() { return *_indexBuffer; }

		string GetName() const { return _name; }

		VkIndexType GetIndexType() const { return _indexType; }

		bool HasVertexAttribute(string attributeName) const;

		// True once legacy (non-GPU-driven) vertex buffers have been built. Used by
		// the loader to skip rebuilding a SubMesh that was already jobified, now that
		// the pool owns it for the app's lifetime (shared_ptr use_count no longer works).
		bool HasBuffers() const { return !_vertexBuffers.empty(); }

		// Returns the owning slot for a transfer job to fill in.
		unique_ptr<Buffer>& InsertBufferSpace(string name);
		unique_ptr<Buffer>& InsertBufferSpace(VkIndexType indexType);

		void SetAllocation(const MeshAllocation& allocation) 
		{ 
			_globalAllocation = allocation; 
			_hasGlobalAllocation = true; 
		}
		const MeshAllocation& GetAllocation() const { return _globalAllocation; }
		bool HasAllocation() const { return _hasGlobalAllocation; }
	private:
		Device& _device;

		string _name;

		uint32_t _indexCount;
		VkIndexType _indexType;

		//Legacy Buffers
		//Key: Attribute name, Value: Attribute value
		unordered_map<string, unique_ptr<Buffer>> _vertexBuffers;
		unique_ptr<Buffer> _indexBuffer;
		
		//Global Buffer Allocation Info
		MeshAllocation _globalAllocation;
		bool _hasGlobalAllocation = false;
	};
}