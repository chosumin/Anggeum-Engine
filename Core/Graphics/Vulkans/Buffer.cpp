#include "stdafx.h"
#include "Buffer.h"
#include "CommandBuffer.h"
#include "MemoryAllocator.h"

Core::Buffer::Buffer(Device& device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
	:_device(device), _size(size)
{
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	auto deviceHandle = _device.GetDevice();

	if (vkCreateBuffer(deviceHandle, &bufferInfo, nullptr, &_buffer) != VK_SUCCESS) {
		throw std::runtime_error("failed to create buffer!");
	}

	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(deviceHandle, _buffer, &memRequirements);

	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex =
		_device.FindMemoryType(memRequirements.memoryTypeBits, properties);

	if (vkAllocateMemory(deviceHandle, &allocInfo, nullptr, &_bufferMemory) != VK_SUCCESS) {
		throw std::runtime_error("failed to allocate buffer memory!");
	}

	vkBindBufferMemory(deviceHandle, _buffer, _bufferMemory, 0);
}

Core::Buffer::Buffer(Device& device, VkDeviceSize size, VkBufferUsageFlags usage, MemoryType memoryType)
	:_device(device), _size(size)
{
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	auto deviceHandle = _device.GetDevice();

	if (vkCreateBuffer(deviceHandle, &bufferInfo, nullptr, &_buffer) != VK_SUCCESS) {
		throw std::runtime_error("failed to create buffer!");
	}

	_allocator = device.GetMemoryAllocator(memoryType);

	_allocation = make_unique<MemoryAllocation>();
	_allocator->Allocate(*_allocation , *this);
}

Core::Buffer::~Buffer()
{
	auto device = _device.GetDevice();

	vkDestroyBuffer(device, _buffer, nullptr);

	if (_allocator == nullptr)
		vkFreeMemory(device, _bufferMemory, nullptr);
	else
	{
		_allocator->Deallocate(*_allocation);
	}
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
	vkMapMemory(
		_device.GetDevice(), _bufferMemory,
		0, size, 0, data);
}