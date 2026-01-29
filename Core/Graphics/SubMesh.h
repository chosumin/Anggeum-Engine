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

		Buffer** InsertBufferSpace(string name);
		Buffer** InsertBufferSpace(VkIndexType indexType);

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
		unordered_map<string, Buffer*> _vertexBuffers;
		Buffer* _indexBuffer;
		
		//Global Buffer Allocation Info
		MeshAllocation _globalAllocation;
		bool _hasGlobalAllocation = false;
	};
}