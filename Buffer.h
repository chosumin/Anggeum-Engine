#pragma once

namespace Core
{
	class CommandBuffer;
	class Buffer
	{
	public:
		Buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
		~Buffer();

		VkBuffer GetBuffer() { return _buffer; }
		VkDeviceMemory GetBufferMemory() { return _bufferMemory; }

		void CopyBuffer(const VkCommandPool& commandPool, VkBuffer srcBuffer, VkDeviceSize size);
		void CopyBuffer(void* data, VkDeviceSize size);
	private:
		void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
			VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
	protected:
		VkBuffer _buffer;
		VkDeviceMemory _bufferMemory;
	};
}

