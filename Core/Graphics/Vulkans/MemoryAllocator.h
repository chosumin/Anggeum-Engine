#pragma once

namespace Core
{
	enum class MemoryType
	{
		STAGE, DEVICE_LOCAL, UNIFORM
	};

	struct MemoryAllocation
	{
		size_t id;
		VkDeviceSize size;
		VkDeviceSize offset;
	};

	class Buffer;
	class Image;
	class Device;
	class MemoryAllocator
	{
	private:
		struct SpanIndexPair { size_t blockIndex; size_t spanIndex; };

		struct OffsetSizePair { uint64_t offset; VkDeviceSize size; };

		struct MemoryBlock
		{
			VkDeviceMemory memory;
			VkDeviceSize size;
			vector<OffsetSizePair> freeMemories;
		};
	public:
		MemoryAllocator(Device& device, VkDeviceSize size, 
			VkMemoryRequirements memRequirements, VkMemoryPropertyFlags properties);
		~MemoryAllocator();

		void Allocate(MemoryAllocation& outAllocation, Buffer& buffer);
		void Deallocate(MemoryAllocation& allocation);
	private:
		bool FindFreeChunkForAllocation(
			SpanIndexPair& indexPair, VkDeviceSize size, bool needsWholePage);
		uint32_t AddBlock(VkDeviceSize size, bool fitToAlloc);
		void MarkChunkOfMemoryBlockUsed(SpanIndexPair indices, VkDeviceSize size);
	private:
		Device& _device;
		size_t _totalAllocSize;
		VkMemoryRequirements _requirements;
		VkDeviceSize _blockMinSize;
		vector<MemoryBlock> _blocks;
		VkDeviceSize _alignment;
		uint32_t _memoryType;
	};
}