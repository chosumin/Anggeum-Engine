#include "stdafx.h"
#include "Graphics/SubMesh.h"
#include "Core/Graphics/Vulkans/Buffer.h"

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

void Core::SubMesh::CreateVertexBuffer(string name, vector<uint8_t>& vertexData)
{
	VkDeviceSize bufferSize = sizeof(vertexData[0]) * vertexData.size();

	Core::Buffer stagingBuffer(_device, bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	stagingBuffer.CopyBuffer(vertexData.data(), bufferSize);

	//todo : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT to use compute shader.
	auto vertexBuffer = new Core::Buffer(_device, bufferSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//vertex memory is moved from CPU to GPU
	vertexBuffer->CopyBuffer(stagingBuffer.GetBuffer(), bufferSize);

	_vertexBuffers[name] = vertexBuffer;
}

void Core::SubMesh::CreateIndexBuffer(vector<uint8_t>& indexData, VkIndexType indexType)
{
	VkDeviceSize bufferSize = sizeof(indexData[0]) * indexData.size();

	Core::Buffer stagingBuffer(_device, bufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	stagingBuffer.CopyBuffer(indexData.data(), bufferSize);

	_indexBuffer = new Core::Buffer(_device, bufferSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	_indexBuffer->CopyBuffer(stagingBuffer.GetBuffer(), bufferSize);

	_indexType = indexType;
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