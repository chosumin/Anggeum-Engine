#include "stdafx.h"
#include "MemoryAllocator.h"
#include "Buffer.h"
#include "Image.h"

Core::MemoryAllocator::MemoryAllocator(Device& device, VkDeviceSize size, 
	VkMemoryRequirements memRequirements, VkMemoryPropertyFlags properties)
	:_device(device), _blockMinSize(size), _requirements(memRequirements)
{
	VkPhysicalDeviceProperties deviceProperties;
	vkGetPhysicalDeviceProperties(_device.GetPhysicalDevice(), &deviceProperties);

	_pageSize = deviceProperties.limits.bufferImageGranularity;
	_blockMinSize = size;

	_memoryType = _device.FindMemoryType(_requirements.memoryTypeBits, properties);
}

Core::MemoryAllocator::~MemoryAllocator()
{
	for (auto&& block : _blocks)
	{
		vkFreeMemory(_device.GetDevice(), block.memory, nullptr);
	}
}

Core::MemorySpanIndex Core::MemoryAllocator::Allocate(Buffer& buffer)
{
	VkDeviceSize size = buffer.GetSize();
	VkDeviceSize requestedAllocSize = ((size / _pageSize) + 1) * _pageSize;
	_totalAllocSize += requestedAllocSize;

	MemorySpanIndex location;

	bool needsOwnPage = false;
	bool found = FindFreeChunkForAllocation(location, requestedAllocSize, needsOwnPage);

	if (found == false)
	{
		location = { AddBlock(requestedAllocSize, needsOwnPage), 0 };
	}

	auto& block = _blocks[location.blockIndex];

	vkBindBufferMemory(_device.GetDevice(), buffer.GetBuffer(),
		block.memory,
		block.layout[location.spanIndex].offset);

	MarkChunkOfMemoryBlockUsed(location, requestedAllocSize);

	return location;
}

void Core::MemoryAllocator::Deallocate(VkDeviceSize size, MemorySpanIndex& memorySpanIndex)
{
	VkDeviceSize requestedAllocSize = ((size / _pageSize) + 1) * _pageSize;

	MemoryBlock block = _blocks[memorySpanIndex.blockIndex];

	OffsetSize span = { block.layout[memorySpanIndex.spanIndex].offset, requestedAllocSize};

	bool found = false;

	for (auto&& layout : block.layout)
	{
		if (layout.offset == requestedAllocSize + span.offset)
		{
			layout.offset = span.offset;
			layout.size += requestedAllocSize;
			found = true;

			break;
		}
	}

	if (found == false)
	{
		block.layout.emplace_back(span);
		_totalAllocSize -= requestedAllocSize;
	}
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
	allocInfo.allocationSize = newPoolSize;
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
	newBlock.layout.emplace_back(0, size);

	_blocks.push_back(newBlock);

	return static_cast<uint32_t>(_blocks.size() - 1);
}

void Core::MemoryAllocator::MarkChunkOfMemoryBlockUsed(MemorySpanIndex indices, VkDeviceSize size)
{
	/*auto& offsetSize = _blocks[indices.blockIndex].layout[indices.spanIndex];
	offsetSize.offset += size;
	offsetSize.size -= size;*/
}
