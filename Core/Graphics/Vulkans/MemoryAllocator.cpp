#include "stdafx.h"
#include "MemoryAllocator.h"
#include "Buffer.h"
#include "Image.h"

Core::MemoryAllocator::MemoryAllocator(Device& device, MemoryType type, 
	VkDeviceSize size, VkMemoryRequirements memRequirements, VkMemoryPropertyFlags properties)
	:_device(device), _blockMinSize(size), _requirements(memRequirements),
	_totalAllocSize(0), _allocatorType(type), _idCounter(0)
{
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

void Core::MemoryAllocator::Allocate(MemoryAllocation& outAllocation, 
	VkDeviceSize size, bool needDedicated)
{
	VkDeviceSize requestedAllocSize = ((size / _alignment) + 1) * _alignment;
	_totalAllocSize += requestedAllocSize;

	SpanIndexPair location;

	if (needDedicated)
	{
		location = { AddBlock(requestedAllocSize, true), 0 };
	}
	else
	{
		bool found = FindFreeChunkForAllocation(location, requestedAllocSize, false);

		if (found == false)
		{
			location = { AddBlock(requestedAllocSize, false), 0 };
		}
	}

	auto& block = _blocks[location.blockIndex];

	outAllocation.id = location.blockIndex;
	outAllocation.size = requestedAllocSize;
	outAllocation.offset = block.freeMemories[location.spanIndex].offset;

	MarkChunkOfMemoryBlockUsed(location, requestedAllocSize);
}

void Core::MemoryAllocator::Deallocate(MemoryAllocation& allocation)
{
	OffsetSizePair span = { allocation.offset , allocation.size };

	auto block = FindMemoryBlock(allocation.id);

	if (block->dedicated)
	{
		vkFreeMemory(_device.GetDevice(), block->memory, nullptr);
		_blocks.erase(block);
	}
	else
	{
		bool found = false;
		for (auto&& freeMemory : block->freeMemories)
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
			block->freeMemories.emplace_back(span);
			_totalAllocSize -= allocation.size;
		}

		//todo : merge when multiple blocks are in sequence
	}
}

void Core::MemoryAllocator::CopyBuffer(void* srcData, MemoryAllocation& allocation)
{
	auto device = _device.GetDevice();

	void* tempData;

	auto& block = *FindMemoryBlock(allocation.id);

	vkMapMemory(device, block.memory, 
		allocation.offset, allocation.size, 0, &tempData);
	memcpy(tempData, srcData, (size_t)allocation.size);
	vkUnmapMemory(device, block.memory);
}

void Core::MemoryAllocator::GetMappedPtr(void** outMappedPtr, MemoryAllocation& allocation)
{
	auto& block = *FindMemoryBlock(allocation.id);

	uint8_t* mappedPtr = static_cast<uint8_t*>(block.mapped);
	*outMappedPtr = mappedPtr + allocation.offset;
}

void Core::MemoryAllocator::BindBufferMemory(Buffer& buffer, MemoryAllocation& allocation)
{
	auto& block = *FindMemoryBlock(allocation.id);

	vkBindBufferMemory(_device.GetDevice(), buffer.GetBuffer(),
		block.memory, allocation.offset);
}

void Core::MemoryAllocator::BindImageMemory(Image& image, MemoryAllocation& allocation)
{
	auto& block = *FindMemoryBlock(allocation.id);

	vkBindImageMemory(_device.GetDevice(), image.GetImage(),
		block.memory, allocation.offset);
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
				indexPair.blockIndex = block.id;
				indexPair.spanIndex = j;

				return true;
			}
		}
	}

	return false;
}

uint32_t Core::MemoryAllocator::AddBlock(VkDeviceSize size, bool needDedicated)
{
	VkDeviceSize newPoolSize = needDedicated ? 
		size : glm::max(size, _blockMinSize);

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

	if (_allocatorType == MemoryType::UNIFORM)
	{
		//persistent mapping
		//The uniform data will be used for all draw calls, 
		//so the buffer containing it should only be destroyed when we stop rendering.

		vkMapMemory(_device.GetDevice(), newBlock.memory,
			0, newPoolSize, 0, &newBlock.mapped);
	}

	newBlock.size = newPoolSize;
	newBlock.freeMemories.push_back({ 0, newPoolSize });
	newBlock.dedicated = needDedicated;
	newBlock.id = _idCounter++;

	_blocks.push_back(newBlock);

	return newBlock.id;
}

void Core::MemoryAllocator::MarkChunkOfMemoryBlockUsed(SpanIndexPair indices, VkDeviceSize size)
{
	auto& block = *FindMemoryBlock(indices.blockIndex);

	auto& offsetSize = block.freeMemories[indices.spanIndex];
	offsetSize.offset += size;
	offsetSize.size -= size;
}

vector<Core::MemoryAllocator::MemoryBlock>::iterator Core::MemoryAllocator::FindMemoryBlock(size_t id)
{
	auto it = find_if(_blocks.begin(), _blocks.end(),
	[id](MemoryBlock& block)->bool
	{
		return block.id == id;
	});

	return it;
}
