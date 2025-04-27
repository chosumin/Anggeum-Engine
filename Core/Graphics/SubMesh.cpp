#include "stdafx.h"
#include "Graphics/SubMesh.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/MemoryAllocator.h"

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

	Core::Buffer stagingBuffer(_device, bufferSize, 
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		MemoryType::STAGE);

	stagingBuffer.CopyBuffer(vertexData.data(), bufferSize);

	auto vertexBuffer = new Core::Buffer(_device, bufferSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		MemoryType::DEVICE_LOCAL);

	auto& buffer = _device.BeginSingleTimeCommands();

	buffer.CopyBuffer(stagingBuffer, *vertexBuffer);

	_device.EndSingleTimeCommands(buffer);

	_vertexBuffers[name] = vertexBuffer;
}

void Core::SubMesh::CreateIndexBuffer(vector<uint8_t>& indexData, VkIndexType indexType)
{
	VkDeviceSize bufferSize = sizeof(indexData[0]) * indexData.size();

	Core::Buffer stagingBuffer(_device, bufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		MemoryType::STAGE);

	stagingBuffer.CopyBuffer(indexData.data(), bufferSize);

	_indexBuffer = new Core::Buffer(_device, bufferSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		MemoryType::DEVICE_LOCAL);

	auto& buffer = _device.BeginSingleTimeCommands();

	buffer.CopyBuffer(stagingBuffer, *_indexBuffer);

	_device.EndSingleTimeCommands(buffer);

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