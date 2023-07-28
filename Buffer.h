#pragma once

namespace Core
{
	class Buffer
	{
	public:
		Buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
		~Buffer();

		VkBuffer GetBuffer() { return _buffer; }
		VkDeviceMemory GetBufferMemory() { return _bufferMemory; }

		void CopyBuffer(VkCommandPool commandPool, VkBuffer srcBuffer, VkDeviceSize size);
		void CopyBuffer(void* data, VkDeviceSize size);
	private:
		void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
			VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
		uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
	protected:
		VkBuffer _buffer;
		VkDeviceMemory _bufferMemory;
	};
}

