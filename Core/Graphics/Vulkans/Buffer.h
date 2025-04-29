#pragma once

namespace Core
{
	struct MemoryAllocation;
	class CommandBuffer;
	class CommandPool;
	class Buffer
	{
	public:
		Buffer(Device& device, VkDeviceSize size, VkBufferUsageFlags usage, MemoryType memoryType);
		~Buffer();

		VkBuffer GetBuffer() { return _buffer; }

		void CopyBuffer(void* data, VkDeviceSize size);
		void GetMappedPtr(void** data);

		VkDeviceSize GetSize() const { return _size; }
	protected:
		Device& _device;
		VkBuffer _buffer;
		VkDeviceSize _size;
		unique_ptr<MemoryAllocation> _allocation;
		MemoryAllocatorManager* _allocator;
	};
}

