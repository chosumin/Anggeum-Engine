#include "stdafx.h"
#include "Triangle.h"
#include "Vertex.h"
#include "CommandBuffer.h"
#include "Buffer.h"

Triangle::Triangle(VkCommandPool commandPool)
{
    _vertices.push_back({ {-0.5f, -0.5f}, { 1.0f, 0.0f, 0.0f }, {1.0f, 0.0f} });
    _vertices.push_back({ {0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f} });
    _vertices.push_back({ {0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f} });
	_vertices.push_back({ {-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f} });

	CreateVertexBuffer(commandPool);
	CreateIndexBuffer(commandPool);
}

Triangle::~Triangle()
{
	delete(_indexBuffer);
	delete(_vertexBuffer);
}

void Triangle::DrawFrame(VkCommandBuffer commandBuffer) const
{
    VkBuffer vertexBuffers[] = { _vertexBuffer->GetBuffer()};
    VkDeviceSize offsets[] = { 0 };

	//hack : optimizable with https://developer.nvidia.com/vulkan-memory-management
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
	
	vkCmdBindIndexBuffer(commandBuffer, _indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT16);

	vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(_indices.size()), 1, 0, 0, 0);
}

void Triangle::CreateVertexBuffer(VkCommandPool commandPool)
{
	VkDeviceSize bufferSize = sizeof(_vertices[0]) * _vertices.size();

	Core::Buffer stagingBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	stagingBuffer.CopyBuffer(_vertices.data(), bufferSize);

	_vertexBuffer = new Core::Buffer(bufferSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	//vertex memory is moved from CPU to GPU
	_vertexBuffer->CopyBuffer(commandPool, stagingBuffer.GetBuffer(), bufferSize);
}

void Triangle::CreateIndexBuffer(VkCommandPool commandPool)
{
	VkDeviceSize bufferSize = sizeof(_indices[0]) * _indices.size();

	Core::Buffer stagingBuffer(bufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	stagingBuffer.CopyBuffer(_indices.data(), bufferSize);

	_indexBuffer = new Core::Buffer(bufferSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	_indexBuffer->CopyBuffer(commandPool, stagingBuffer.GetBuffer(), bufferSize);
}