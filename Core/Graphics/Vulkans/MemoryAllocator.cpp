#include "stdafx.h"
#include "MemoryAllocator.h"
#include "Buffer.h"
#include "Image.h"

Core::MemoryAllocator::MemoryAllocator(Device& device, VkDeviceSize size, VkMemoryPropertyFlags properties)
	:_device(device), _blockMinSize(size)
{
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(_device.GetPhysicalDevice(), &memProperties);

	VkPhysicalDeviceProperties deviceProperties;
	vkGetPhysicalDeviceProperties(_device.GetPhysicalDevice(), &deviceProperties);

	_pageSize = deviceProperties.limits.bufferImageGranularity;
	_blockMinSize = _pageSize * 10;

	_memoryType = _device.FindMemoryType(_requirements.memoryTypeBits, properties);

	VkBuffer buffer{};
	vkGetBufferMemoryRequirements(device.GetDevice(), buffer, &_requirements);
}

Core::MemoryAllocator::~MemoryAllocator()
{
	for (auto&& block : _blocks)
	{
		vkFreeMemory(_device.GetDevice(), block.memory, nullptr);
	}
}

void Core::MemoryAllocator::Allocate(Buffer& buffer)
{
	VkDeviceSize size = buffer.GetSize();
	VkDeviceSize requestedAllocSize = ((size / _pageSize) + 1) * _pageSize;
	_totalSize += requestedAllocSize;

	MemorySpanIndex location;

	bool needsOwnPage = false;
	bool found = FindFreeChunkForAllocation(location, requestedAllocSize, needsOwnPage);

	if (found == false)
	{
		location = { AddBlock(requestedAllocSize, needsOwnPage), 0 };
	}

	buffer.BindMemory(location.blockIndex, location.spanIndex);

	MarkChunkOfMemoryBlockUsed(location, requestedAllocSize);

	return false;
}

bool Core::MemoryAllocator::Deallocate(Buffer const& block)
{
	return false;
}

int Core::MemoryAllocator::GetMemoryType() const
{
	return 0;
}

bool Core::MemoryAllocator::FindFreeChunkForAllocation(MemorySpanIndex& indexPair, VkDeviceSize size, bool needsWholePage)
{
	for (size_t i = 0; i < _blocks.size(); ++i)
	{
		auto block = _blocks[i];

		for (size_t j = 0; j < block.layout.size(); ++j)
		{
			auto offsetSize = block.layout[j];

			bool validOffset = needsWholePage ? offsetSize.offset == 0 : true;

			if (offsetSize.size >= size && validOffset)
			{
				indexPair.blockIndex = i;
				indexPair.spanIndex = j;

				return true;
			}
		}
	}

	return false;
}

uint32_t Core::MemoryAllocator::AddBlock(VkDeviceSize size, bool fitToAlloc)
{
	VkDeviceSize newPoolSize = size * 2;
	newPoolSize = glm::max(newPoolSize, _blockMinSize);

	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = _requirements.size;
	allocInfo.memoryTypeIndex = _memoryType;

	MemoryBlock newBlock{};

	if (vkAllocateMemory(_device.GetDevice(),
		&allocInfo,
		nullptr,
		&newBlock.memory) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to allocate buffer memory!");
	}

	newBlock.size = newPoolSize;

	newBlock.layout.emplace_back(0, newPoolSize);

	return static_cast<uint32_t>(_blocks.size() - 1);
}

void Core::MemoryAllocator::MarkChunkOfMemoryBlockUsed(MemorySpanIndex indices, VkDeviceSize size)
{

}
