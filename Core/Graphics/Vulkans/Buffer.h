#pragma once

namespace Core
{
	struct MemorySpanIndex;
	class CommandBuffer;
	class CommandPool;
	class Buffer
	{
	public:
		Buffer(Device& device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
		Buffer(Device& device, VkDeviceSize size, VkBufferUsageFlags usage, MemoryType memoryType);
		~Buffer();

		VkBuffer GetBuffer() { return _buffer; }
		VkDeviceMemory GetBufferMemory() { return _bufferMemory; }

		void CopyBuffer(VkBuffer srcBuffer, VkDeviceSize size);
		void CopyBuffer(void* data, VkDeviceSize size);
		void MapMemory(void** data, VkDeviceSize size);

		VkDeviceSize GetSize() const { return _size; }
	protected:
		Device& _device;
		VkBuffer _buffer;
		VkDeviceMemory _bufferMemory = VK_NULL_HANDLE;
		VkDeviceSize _size;
		MemorySpanIndex _allocation;
		MemoryAllocator* _allocator;
	};
}

