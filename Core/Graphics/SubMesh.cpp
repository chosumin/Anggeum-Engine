#include "stdafx.h"
#include "Graphics/SubMesh.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Buffer.h"

Core::SubMesh::SubMesh(Device& device, string name)
	:_device(device), _indexBuffer(nullptr), _name(name)
{
}

Core::SubMesh::~SubMesh()
{
	delete(_indexBuffer);

	for (auto&& vertexBuffer : _vertexBuffers)
	{
		delete(vertexBuffer.second);
	}
	_vertexBuffers.clear();
}

bool Core::SubMesh::HasVertexAttribute(string attributeName) const
{
	auto vertexBuffer = _vertexBuffers.find(attributeName);
	return vertexBuffer != _vertexBuffers.end();
}

Core::Buffer** Core::SubMesh::InsertBufferSpace(string name)
{
	_vertexBuffers[name] = nullptr;
	return &_vertexBuffers[name];
}

Core::Buffer** Core::SubMesh::InsertBufferSpace(VkIndexType indexType)
{
	_indexType = indexType;
	return &_indexBuffer;
}

vector<Core::Buffer*> Core::SubMesh::GetVertexBuffers(vector<string> names) const
{
	vector<Core::Buffer*> buffers;

	for (string name : names)
	{
		auto vertexBuffer = _vertexBuffers.find(name);
		assert(vertexBuffer != _vertexBuffers.end());

		buffers.push_back(vertexBuffer->second);
	}

	return buffers;
}