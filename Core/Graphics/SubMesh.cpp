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

	auto allocatorManager = _device.GetMemoryAllocatorManager();
	Core::Buffer& stagingBuffer = allocatorManager->CreateStagingBuffer(bufferSize);

	stagingBuffer.CopyBuffer(vertexData.data(), bufferSize);

	_vertexStagingBuffers[name] = &stagingBuffer;
}

void Core::SubMesh::CreateIndexBuffer(vector<uint8_t>& indexData, VkIndexType indexType)
{
	VkDeviceSize bufferSize = sizeof(indexData[0]) * indexData.size();

	auto allocatorManager = _device.GetMemoryAllocatorManager();
	Core::Buffer& stagingBuffer = allocatorManager->CreateStagingBuffer(bufferSize);

	stagingBuffer.CopyBuffer(indexData.data(), bufferSize);
	
	_indexStagingBuffer = &stagingBuffer;

	_indexType = indexType;
}

void Core::SubMesh::Load(CommandBuffer& commandBuffer)
{
	for (auto&& stagingBuffer : _vertexStagingBuffers)
	{
		auto vertexBuffer = new Core::Buffer(_device, 
			stagingBuffer.second->GetSize(),
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			MemoryType::DEVICE_LOCAL);

		commandBuffer.CopyBuffer(*stagingBuffer.second, *vertexBuffer);

		_vertexBuffers[stagingBuffer.first] = vertexBuffer;
	}
	
	_vertexStagingBuffers.clear();

	_indexBuffer = new Core::Buffer(_device, _indexStagingBuffer->GetSize(),
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		MemoryType::DEVICE_LOCAL);

	commandBuffer.CopyBuffer(*_indexStagingBuffer, *_indexBuffer);

	_indexStagingBuffer = nullptr;
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