#include "stdafx.h"
#include "MemoryAllocator.h"
#include "Buffer.h"
#include "Image.h"

Core::MemoryAllocator::MemoryAllocator(Device& device, VkDeviceSize size, 
	VkMemoryRequirements memRequirements, VkMemoryPropertyFlags properties)
	:_device(device), _blockMinSize(size), _requirements(memRequirements),
	_totalAllocSize(0)
{
	VkPhysicalDeviceProperties deviceProperties;
	vkGetPhysicalDeviceProperties(_device.GetPhysicalDevice(), &deviceProperties);

	_alignment = _requirements.alignment;
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

void Core::MemoryAllocator::Allocate(MemoryAllocation& outAllocation, Buffer& buffer)
{
	VkDeviceSize size = buffer.GetSize();

	VkDeviceSize requestedAllocSize = ((size / _alignment) + 1) * _alignment;
	_totalAllocSize += requestedAllocSize;

	SpanIndexPair location;

	bool needsOwnPage = false;
	bool found = FindFreeChunkForAllocation(location, requestedAllocSize, needsOwnPage);

	if (found == false)
	{
		location = { AddBlock(requestedAllocSize, needsOwnPage), 0 };
	}

	auto& block = _blocks[location.blockIndex];

	outAllocation.id = location.blockIndex;
	outAllocation.size = requestedAllocSize;
	outAllocation.offset = block.freeMemories[location.spanIndex].offset;

	vkBindBufferMemory(_device.GetDevice(), buffer.GetBuffer(),
		block.memory, outAllocation.offset);

	MarkChunkOfMemoryBlockUsed(location, requestedAllocSize);
}

void Core::MemoryAllocator::Deallocate(MemoryAllocation& allocation)
{
	OffsetSizePair span = { allocation.offset , allocation.size };

	bool found = false;

	MemoryBlock& block = _blocks[allocation.id];
	for (auto&& freeMemory : block.freeMemories)
	{
		if (freeMemory.offset == span.size + span.offset)
		{
			freeMemory.offset = span.offset;
			freeMemory.size += allocation.size;
			found = true;

			break;
		}
	}

	if (found == false)
	{
		block.freeMemories.emplace_back(span);
		_totalAllocSize -= allocation.size;
	}

	//todo : merge when multiple blocks are in sequence
}

bool Core::MemoryAllocator::FindFreeChunkForAllocation(SpanIndexPair& indexPair, VkDeviceSize size, bool needsWholePage)
{
	for (size_t i = 0; i < _blocks.size(); ++i)
	{
		auto block = _blocks[i];

		for (size_t j = 0; j < block.freeMemories.size(); ++j)
		{
			auto offsetSizePair = block.freeMemories[j];

			bool validOffset = needsWholePage ? offsetSizePair.offset == 0 : true;

			if (offsetSizePair.size >= size && validOffset)
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
	VkDeviceSize newPoolSize = glm::max(size, _blockMinSize);

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
	newBlock.freeMemories.push_back({ 0, newPoolSize });

	_blocks.push_back(newBlock);

	return static_cast<uint32_t>(_blocks.size() - 1);
}

void Core::MemoryAllocator::MarkChunkOfMemoryBlockUsed(SpanIndexPair indices, VkDeviceSize size)
{
	auto& offsetSize = _blocks[indices.blockIndex].freeMemories[indices.spanIndex];
	offsetSize.offset += size;
	offsetSize.size -= size;
}
