#pragma once

namespace Core
{
	enum class MemoryType
	{
		STAGE, DEVICE_LOCAL, UNIFORM
	};

	struct MemorySpanIndex
	{
		size_t blockIndex;
		size_t spanIndex;
	};

	class Buffer;
	class Image;
	class Device;
	class MemoryAllocator
	{
	private:
		struct OffsetSize
		{
			uint64_t offset;
			VkDeviceSize size;

			OffsetSize(uint64_t offset, VkDeviceSize size)
				:offset(offset), size(size) 
			{

			}
		};

		struct MemoryBlock
		{
			VkDeviceMemory memory;
			uint32_t id;
			VkDeviceSize size;
			vector<OffsetSize> layout;
		};
	public:
		MemoryAllocator(Device& device, VkDeviceSize size, 
			VkMemoryRequirements memRequirements, VkMemoryPropertyFlags properties);
		~MemoryAllocator();

		MemorySpanIndex Allocate(Buffer& buffer);
		void Deallocate(VkDeviceSize size, MemorySpanIndex& spanIndex);
	private:
		bool FindFreeChunkForAllocation(
			MemorySpanIndex& indexPair, VkDeviceSize size, bool needsWholePage);
		uint32_t AddBlock(VkDeviceSize size, bool fitToAlloc);
		void MarkChunkOfMemoryBlockUsed(MemorySpanIndex indices, VkDeviceSize size);
	private:
		Device& _device;
		size_t _totalAllocSize = 0;
		VkMemoryRequirements _requirements;
		VkDeviceSize _blockMinSize;
		vector<MemoryBlock> _blocks;
		VkDeviceSize _pageSize;
		uint32_t _memoryType;
	};
}