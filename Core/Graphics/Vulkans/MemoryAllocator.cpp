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
	lock_guard<mutex> lock(_mutex);

	// Round UP to the alignment (exact multiples stay as they are); the
	// reservation may still exceed `size`, so copies must size from the data.
	VkDeviceSize requestedAllocSize = ((size + _alignment - 1) / _alignment) * _alignment;
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

	outAllocation.id = block.id;
	outAllocation.size = requestedAllocSize;
	outAllocation.offset = block.freeMemories[location.spanIndex].offset;
	outAllocation.type = _allocatorType;

	MarkChunkOfMemoryBlockUsed(location, requestedAllocSize);
}

void Core::MemoryAllocator::Deallocate(MemoryAllocation& allocation)
{
	lock_guard<mutex> lock(_mutex);

	OffsetSizePair span = { allocation.offset , allocation.size };

	auto block = FindMemoryBlock(allocation.id);
	assert(block != _blocks.end() && "deallocating from an unknown block");

	_totalAllocSize -= allocation.size;

	if (block->dedicated)
	{
		vkFreeMemory(_device.GetDevice(), block->memory, nullptr);
		_blocks.erase(block);
	}
	else
	{
		// Coalesce with both neighbours: `backward` ends where the freed span
		// starts, `forward` starts where it ends. Merging only one direction
		// would fragment the free list under load/unload churn.
		auto& freeList = block->freeMemories;
		auto backward = find_if(freeList.begin(), freeList.end(),
			[&](const OffsetSizePair& f) { return f.offset + f.size == span.offset; });
		auto forward = find_if(freeList.begin(), freeList.end(),
			[&](const OffsetSizePair& f) { return f.offset == span.offset + span.size; });

		if (backward != freeList.end() && forward != freeList.end())
		{
			// The freed span bridges two free spans into one.
			backward->size += span.size + forward->size;
			freeList.erase(forward);
		}
		else if (backward != freeList.end())
		{
			backward->size += span.size;
		}
		else if (forward != freeList.end())
		{
			forward->offset = span.offset;
			forward->size += span.size;
		}
		else
		{
			freeList.emplace_back(span);
		}
	}
}

void Core::MemoryAllocator::CopyBuffer(void* srcData, MemoryAllocation& allocation, VkDeviceSize size)
{
	// Copy the caller's byte count, NOT allocation.size: allocations are rounded up to
	// the alignment, so the reservation is always larger than the data behind srcData.
	assert(size <= allocation.size && "copy larger than the allocation");

	// Resolve the block under the lock (AddBlock may relocate the vector), but
	// memcpy outside it: the mapping itself is stable for the block's lifetime.
	uint8_t* mapped = nullptr;
	{
		lock_guard<mutex> lock(_mutex);
		auto& block = *FindMemoryBlock(allocation.id);
		if (block.mapped != nullptr)
			mapped = static_cast<uint8_t*>(block.mapped) + allocation.offset;
	}

	// The host-visible pool (UNIFORM) is persistently mapped; a copy into an
	// unmapped (device-local) allocation is a caller bug.
	assert(mapped != nullptr && "CopyBuffer into a non-host-visible pool");

	memcpy(mapped, srcData, (size_t)size);
}

void Core::MemoryAllocator::GetMappedPtr(void** outMappedPtr, MemoryAllocation& allocation)
{
	// Only the persistently mapped allocator (UNIFORM) populates block.mapped;
	// device-local pools return garbage here.
	auto& block = *FindMemoryBlock(allocation.id);

	uint8_t* mappedPtr = static_cast<uint8_t*>(block.mapped);
	*outMappedPtr = mappedPtr + allocation.offset;
}

void Core::MemoryAllocator::BindBufferMemory(Buffer& buffer, MemoryAllocation& allocation)
{
	lock_guard<mutex> lock(_mutex);

	auto& block = *FindMemoryBlock(allocation.id);

	vkBindBufferMemory(_device.GetDevice(), buffer.GetBuffer(),
		block.memory, allocation.offset);
}

void Core::MemoryAllocator::BindImageMemory(Image& image, MemoryAllocation& allocation)
{
	lock_guard<mutex> lock(_mutex);

	auto& block = *FindMemoryBlock(allocation.id);

	vkBindImageMemory(_device.GetDevice(), image.GetImage(),
		block.memory, allocation.offset);
}

bool Core::MemoryAllocator::FindFreeChunkForAllocation(SpanIndexPair& indexPair, VkDeviceSize size, bool needsWholePage)
{
	for (size_t i = 0; i < _blocks.size(); ++i)
	{
		auto& block = _blocks[i];

		for (size_t j = 0; j < block.freeMemories.size(); ++j)
		{
			const auto& offsetSizePair = block.freeMemories[j];

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

	if (_allocatorType == MemoryType::UNIFORM
		|| _allocatorType == MemoryType::DEDICATED_HOST)
	{
		// Host-visible blocks are persistently mapped: a single map per block
		// means concurrent users can never double-map it
		// (VUID-vkMapMemory-memory-00678).
		vkMapMemory(_device.GetDevice(), newBlock.memory,
			0, newPoolSize, 0, &newBlock.mapped);
	}

	newBlock.size = newPoolSize;
	newBlock.freeMemories.push_back({ 0, newPoolSize });
	newBlock.dedicated = needDedicated;
	newBlock.id = _idCounter++;

	_blocks.push_back(newBlock);

	// The new block's VECTOR index (SpanIndexPair::blockIndex), not its id.
	return static_cast<uint32_t>(_blocks.size() - 1);
}

void Core::MemoryAllocator::MarkChunkOfMemoryBlockUsed(SpanIndexPair indices, VkDeviceSize size)
{
	auto& block = _blocks[indices.blockIndex];

	auto& offsetSize = block.freeMemories[indices.spanIndex];
	offsetSize.offset += size;
	offsetSize.size -= size;

	// Fully consumed spans would otherwise linger as zero-size entries the
	// free-chunk scan keeps visiting (and Deallocate could merge against).
	if (offsetSize.size == 0)
		block.freeMemories.erase(block.freeMemories.begin() + indices.spanIndex);
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

Core::MemoryAllocatorManager::MemoryAllocatorManager(Device& device)
	:_device(device)
{
	auto deviceHandle = device.GetDevice();

	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = 1;
	bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkBuffer dummyBuffer;

	if (vkCreateBuffer(deviceHandle, &bufferInfo, nullptr, &dummyBuffer) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create buffer!");
	}

	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(deviceHandle, dummyBuffer, &memRequirements);

	uint32_t memoryType = memRequirements.memoryTypeBits;

	_memoryAllocators[MemoryType::DEVICE_LOCAL] = new MemoryAllocator(device, MemoryType::DEVICE_LOCAL,
		32 * 1024 * 1024, memRequirements,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	_memoryAllocators[MemoryType::UNIFORM] = new MemoryAllocator(device, MemoryType::UNIFORM,
		16 * 1024 * 1024, memRequirements,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	// Not a pool: every allocation gets (and frees) its own dedicated block,
	// so the block-min size is irrelevant.
	_memoryAllocators[MemoryType::DEDICATED_HOST] = new MemoryAllocator(device,
		MemoryType::DEDICATED_HOST, 0, memRequirements,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	vkDestroyBuffer(deviceHandle, dummyBuffer, nullptr);

	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.extent = { 1024, 1024, 1 };
	imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;

	VkImage dummyImage;

	if (vkCreateImage(deviceHandle, &imageInfo, nullptr, &dummyImage) != VK_SUCCESS)
	{
		throw runtime_error("failed to create image!");
	}

	VkMemoryRequirements imageMemRequirements;
	vkGetImageMemoryRequirements(deviceHandle, dummyImage, &imageMemRequirements);

	_memoryAllocators[MemoryType::IMAGE] = new MemoryAllocator(_device, MemoryType::IMAGE,
		128 * 1024 * 1024, imageMemRequirements,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	vkDestroyImage(deviceHandle, dummyImage, nullptr);
}

Core::MemoryAllocatorManager::~MemoryAllocatorManager()
{
	for (auto&& allocator : _memoryAllocators)
	{
		delete(allocator.second);
	}
}

Core::MemoryAllocator* Core::MemoryAllocatorManager::GetMemoryAllocator(MemoryType memoryType) const
{
	auto iter = _memoryAllocators.find(memoryType);
	if (iter != _memoryAllocators.end())
	{
		return iter->second;
	}

	throw runtime_error("failed to find the matched memory allocator!");
}

void Core::MemoryAllocatorManager::Allocate(MemoryAllocation& outAllocation, MemoryType type, VkDeviceSize size, bool needDedicated)
{
	// DEDICATED_HOST is dedicated by definition; the caller states it
	// explicitly (a pooled allocation here would never be block-freed).
	assert((type != MemoryType::DEDICATED_HOST || needDedicated)
		&& "DEDICATED_HOST allocations must pass needDedicated");

	_memoryAllocators[type]->Allocate(outAllocation, size, needDedicated);
}

void Core::MemoryAllocatorManager::Deallocate(MemoryAllocation& allocation)
{
	_memoryAllocators[allocation.type]->Deallocate(allocation);
}

void Core::MemoryAllocatorManager::BindBufferMemory(Buffer& buffer, MemoryAllocation& allocation)
{
	_memoryAllocators[allocation.type]->BindBufferMemory(buffer, allocation);
}

void Core::MemoryAllocatorManager::BindImageMemory(Image& image, MemoryAllocation& allocation)
{
	_memoryAllocators[allocation.type]->BindImageMemory(image, allocation);
}

void Core::MemoryAllocatorManager::GetMappedPtr(void** outMappedPtr, MemoryAllocation& allocation)
{
	_memoryAllocators[allocation.type]->GetMappedPtr(outMappedPtr, allocation);
}

void Core::MemoryAllocatorManager::CopyBuffer(void* srcData, MemoryAllocation& allocation, VkDeviceSize size)
{
	_memoryAllocators[allocation.type]->CopyBuffer(srcData, allocation, size);
}