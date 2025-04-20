#pragma once

namespace Core
{
	struct MemorySpanIndex
	{
		uint32_t blockIndex;
		uint32_t spanIndex;
	};

	struct OffsetSize
	{
		uint64_t offset;
		uint64_t size;
	};

	struct MemoryBlock
	{
		VkDeviceMemory memory;
		uint32_t id;
		VkDeviceSize size;
		vector<OffsetSize> layout;
	};

	class Buffer;
	class Image;
	class MemoryAllocator
	{
	public:
		MemoryAllocator(Device& device, VkDeviceSize size, VkMemoryPropertyFlags properties);
		~MemoryAllocator();

		void Allocate(Buffer& buffer);
		bool Deallocate(Buffer const& block);
		int GetMemoryType() const;
	private:
		bool FindFreeChunkForAllocation(
			MemorySpanIndex& indexPair, VkDeviceSize size, bool needsWholePage);
		uint32_t AddBlock(VkDeviceSize size, bool fitToAlloc);
		void MarkChunkOfMemoryBlockUsed(MemorySpanIndex indices, VkDeviceSize size);
	private:
		Device& _device;
		size_t _totalSize;
		VkMemoryRequirements _requirements;
		VkDeviceSize _blockMinSize;
		vector<MemoryBlock> _blocks;
		uint32_t _pageSize;
		uint32_t _memoryType;
		void* _ptr;
	};
}