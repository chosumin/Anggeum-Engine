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
	outAllocation.type = _allocatorType;

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

	return static_cast<uint32_t>(newBlock.id);
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

	_memoryAllocators[MemoryType::STAGE] = new MemoryAllocator(device, MemoryType::STAGE,
		64 * 1024 * 1024, memRequirements,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	_memoryAllocators[MemoryType::UNIFORM] = new MemoryAllocator(device, MemoryType::UNIFORM,
		16 * 1024 * 1024, memRequirements,
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

void Core::MemoryAllocatorManager::CopyBuffer(void* srcData, MemoryAllocation& allocation)
{
	_memoryAllocators[allocation.type]->CopyBuffer(srcData, allocation);
}