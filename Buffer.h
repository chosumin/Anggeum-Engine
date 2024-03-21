#pragma once

namespace Core
{
	class CommandBuffer;
	class CommandPool;
	class Buffer
	{
	public:
		Buffer(Device& device, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
		~Buffer();

		VkBuffer GetBuffer() { return _buffer; }
		VkDeviceMemory GetBufferMemory() { return _bufferMemory; }

		void CopyBuffer(VkBuffer srcBuffer, VkDeviceSize size);
		void CopyBuffer(void* data, VkDeviceSize size);
		void MapMemory(void** data, VkDeviceSize size);
	private:
		void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
			VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
	protected:
		Device& _device;
		VkBuffer _buffer;
		VkDeviceMemory _bufferMemory;
	};
}

