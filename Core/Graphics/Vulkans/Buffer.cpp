#include "stdafx.h"
#include "Buffer.h"
#include "CommandBuffer.h"
#include "MemoryAllocator.h"

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

	_allocator = device.GetMemoryAllocatorManager();

	_allocation = make_unique<MemoryAllocation>();
	_allocator->Allocate(*_allocation, memoryType, _size, false);
	_allocator->BindBufferMemory(*this, *_allocation);
}

Core::Buffer::~Buffer()
{
	auto device = _device.GetDevice();

	vkDestroyBuffer(device, _buffer, nullptr);

	_allocator->Deallocate(*_allocation);
}

void Core::Buffer::CopyBuffer(void* data, VkDeviceSize size)
{
	_allocator->CopyBuffer(data, *_allocation);
}

void Core::Buffer::GetMappedPtr(void** data)
{
	_allocator->GetMappedPtr(data, *_allocation);
}