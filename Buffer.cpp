#include "stdafx.h"
#include "Buffer.h"
#include "CommandBuffer.h"

Core::Buffer::Buffer(Device& device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
	:_device(device)
{
	CreateBuffer(size, usage, properties, _buffer, _bufferMemory);
}

Core::Buffer::~Buffer()
{
	auto device = _device.GetDevice();

	vkDestroyBuffer(device, _buffer, nullptr);
	vkFreeMemory(device, _bufferMemory, nullptr);
}

void Core::Buffer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory)
{
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	auto device = _device.GetDevice();

	if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
		throw std::runtime_error("failed to create buffer!");
	}

	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = 
		_device.FindMemoryType(memRequirements.memoryTypeBits, properties);

	//hack : use vkAllocateMemory for a large number of objects at once.
	//https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
	if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
		throw std::runtime_error("failed to allocate buffer memory!");
	}

	vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

void Core::Buffer::CopyBuffer(VkBuffer srcBuffer, VkDeviceSize size)
{
	auto& buffer = _device.BeginSingleTimeCommands();
	
	VkBufferCopy copyRegion{};
	copyRegion.srcOffset = 0; // Optional
	copyRegion.dstOffset = 0; // Optional
	copyRegion.size = size;
	vkCmdCopyBuffer(buffer.GetHandle(), srcBuffer, _buffer,
		1, &copyRegion);

	_device.EndSingleTimeCommands(buffer);
}

void Core::Buffer::CopyBuffer(void* data, VkDeviceSize size)
{
	auto device = _device.GetDevice();

	void* tempData;
	vkMapMemory(device, _bufferMemory, 0, size, 0, &tempData);
	memcpy(tempData, data, (size_t)size);
	vkUnmapMemory(device, _bufferMemory);
}

void Core::Buffer::MapMemory(void** data, VkDeviceSize size)
{
	//persistent mapping
		//The uniform data will be used for all draw calls, 
		//so the buffer containing it should only be destroyed when we stop rendering.
	vkMapMemory(
		_device.GetDevice(), _bufferMemory,
		0, size, 0, data);
}

//void Core::Buffer::CopyInstanceBuffer(void* data, VkDeviceSize size)
//{
//	memcpy(_uniformBuffersMapped[currentImage], data, _bufferSize);
//}
