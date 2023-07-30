#include "stdafx.h"
#include "Buffer.h"
#include "Utility.h"

Core::Buffer::Buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties)
{
	CreateBuffer(size, usage, properties, _buffer, _bufferMemory);
}

Core::Buffer::~Buffer()
{
	auto device = Core::Device::Instance().GetDevice();

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

	auto device = Core::Device::Instance().GetDevice();

	if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
		throw std::runtime_error("failed to create buffer!");
	}

	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = 
		Device::Instance().FindMemoryType(memRequirements.memoryTypeBits, properties);

	//hack : use vkAllocateMemory for a large number of objects at once.
	//https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
	if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
		throw std::runtime_error("failed to allocate buffer memory!");
	}

	vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

void Core::Buffer::CopyBuffer(const VkCommandPool& commandPool, VkBuffer srcBuffer, VkDeviceSize size)
{
	auto buffer = Utility::BeginSingleTimeCommands(commandPool);
	
	VkBufferCopy copyRegion{};
	copyRegion.srcOffset = 0; // Optional
	copyRegion.dstOffset = 0; // Optional
	copyRegion.size = size;
	vkCmdCopyBuffer(buffer, srcBuffer, _buffer,
		1, &copyRegion);

	Utility::EndSingleTimeCommands(commandPool, buffer);
}

void Core::Buffer::CopyBuffer(void* data, VkDeviceSize size)
{
	auto device = Core::Device::Instance().GetDevice();

	void* tempData;
	vkMapMemory(device, _bufferMemory, 0, size, 0, &tempData);
	memcpy(tempData, data, (size_t)size);
	vkUnmapMemory(device, _bufferMemory);
}