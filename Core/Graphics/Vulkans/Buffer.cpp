#include "stdafx.h"
#include "Buffer.h"
#include "CommandBuffer.h"

Core::Buffer::Buffer(Device& device, VkDeviceSize size, VkBufferUsageFlags usage, MemoryType memoryType)
	:_device(device), _size(size)
{
	CreateVkBuffer(size, usage);

	_allocator = device.GetMemoryAllocatorManager();

	_allocation = make_unique<MemoryAllocation>();
	_allocator->Allocate(*_allocation, memoryType, _size, false);
	_allocator->BindBufferMemory(*this, *_allocation);
}

Core::Buffer::Buffer(Device& device, VkDeviceSize size, VkBufferUsageFlags usage, Unbound)
	:_device(device), _size(size), _allocator(nullptr)
{
	CreateVkBuffer(size, usage);
}

void Core::Buffer::CreateVkBuffer(VkDeviceSize size, VkBufferUsageFlags usage)
{
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;

	const auto& qfi = _device.GetQueueFamilyIndices();
	uint32_t queueFamilies[2] = {
		qfi.GraphicsFamily.value(),
		qfi.ComputeFamily.value()
	};
	if (qfi.GraphicsFamily.value() != qfi.ComputeFamily.value())
	{
		bufferInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
		bufferInfo.queueFamilyIndexCount = 2;
		bufferInfo.pQueueFamilyIndices = queueFamilies;
	}
	else
	{
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	}

	if (vkCreateBuffer(_device.GetDevice(), &bufferInfo, nullptr, &_buffer) != VK_SUCCESS) {
		throw std::runtime_error("failed to create buffer!");
	}
}

VkMemoryRequirements Core::Buffer::GetMemoryRequirements() const
{
	VkMemoryRequirements requirements{};
	vkGetBufferMemoryRequirements(_device.GetDevice(), _buffer, &requirements);
	return requirements;
}

void Core::Buffer::BindMemoryAt(VkDeviceMemory memory, VkDeviceSize offset)
{
	assert(_allocation == nullptr && "buffer already owns a managed allocation");

	if (vkBindBufferMemory(_device.GetDevice(), _buffer, memory, offset) != VK_SUCCESS)
		throw std::runtime_error("failed to bind buffer memory at offset!");
}

Core::Buffer::~Buffer()
{
	auto device = _device.GetDevice();

	vkDestroyBuffer(device, _buffer, nullptr);

	// Placed buffers (Unbound + BindMemoryAt) do not own their memory.
	if (_allocation != nullptr)
		_allocator->Deallocate(*_allocation);
}

void Core::Buffer::CopyBuffer(void* data, VkDeviceSize size)
{
	_allocator->CopyBuffer(data, *_allocation, size);
}

void Core::Buffer::GetMappedPtr(void** data)
{
	_allocator->GetMappedPtr(data, *_allocation);
}

void Core::Buffer::UpdateRaw(const void* data, VkDeviceSize size)
{
	assert(size <= _size && "update larger than the buffer");

	memcpy(GetPersistentMappedPtr(), data, static_cast<size_t>(size));
}

void* Core::Buffer::GetPersistentMappedPtr()
{
	if (_mapped == nullptr)
	{
		assert((_allocation->type == MemoryType::UNIFORM
			|| _allocation->type == MemoryType::STAGE) &&
			"Update requires a persistently mapped memory type");

		_allocator->GetMappedPtr(&_mapped, *_allocation);
	}

	return _mapped;
}

