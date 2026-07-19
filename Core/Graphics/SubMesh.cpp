#include "stdafx.h"
#include "Graphics/SubMesh.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Buffer.h"

Core::SubMesh::SubMesh(Device& device, string name)
	:_device(device), _name(name)
{
}

Core::SubMesh::~SubMesh() = default;

bool Core::SubMesh::HasVertexAttribute(string attributeName) const
{
	auto vertexBuffer = _vertexBuffers.find(attributeName);
	return vertexBuffer != _vertexBuffers.end();
}

unique_ptr<Core::Buffer>& Core::SubMesh::InsertBufferSpace(string name)
{
	return _vertexBuffers[name];
}

unique_ptr<Core::Buffer>& Core::SubMesh::InsertBufferSpace(VkIndexType indexType)
{
	_indexType = indexType;
	return _indexBuffer;
}

vector<Core::Buffer*> Core::SubMesh::GetVertexBuffers(vector<string> names) const
{
	vector<Core::Buffer*> buffers;

	for (string name : names)
	{
		auto vertexBuffer = _vertexBuffers.find(name);
		assert(vertexBuffer != _vertexBuffers.end());

		buffers.push_back(vertexBuffer->second.get());
	}

	return buffers;
}