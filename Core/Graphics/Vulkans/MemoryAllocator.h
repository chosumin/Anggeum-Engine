#pragma once

//Based on https://kylehalladay.com/blog/tutorial/2017/12/13/Custom-Allocators-Vulkan.html

namespace Core
{
	enum class MemoryType
	{
		STAGE, DEVICE_LOCAL, UNIFORM, IMAGE
	};

	struct MemoryAllocation
	{
		MemoryType type;
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
			size_t id;
			VkDeviceMemory memory;
			VkDeviceSize size;
			void* mapped;
			vector<OffsetSizePair> freeMemories;
			bool dedicated;
		};
	public:
		MemoryAllocator(Device& device, MemoryType type, VkDeviceSize size,
			VkMemoryRequirements memRequirements, VkMemoryPropertyFlags properties);
		~MemoryAllocator();

		void Allocate(MemoryAllocation& outAllocation, VkDeviceSize size, bool needDedicated);
		void Deallocate(MemoryAllocation& allocation);
		void CopyBuffer(void* srcData, MemoryAllocation& allocation, VkDeviceSize size);
		void GetMappedPtr(void** outMappedPtr, MemoryAllocation& allocation);
		void MapMemory(void** outMappedPtr, MemoryAllocation& allocation);
		void UnmapMemory(MemoryAllocation& allocation);
		void BindBufferMemory(Buffer& buffer, MemoryAllocation& allocation);
		void BindImageMemory(Image& image, MemoryAllocation& allocation);
	private:
		bool FindFreeChunkForAllocation(
			SpanIndexPair& indexPair, VkDeviceSize size, bool needsWholePage);
		uint32_t AddBlock(VkDeviceSize size, bool needDedicated);
		void MarkChunkOfMemoryBlockUsed(SpanIndexPair indices, VkDeviceSize size);
		vector<MemoryBlock>::iterator FindMemoryBlock(size_t id);
	private:
		Device& _device;
		size_t _totalAllocSize;
		VkMemoryRequirements _requirements;
		VkDeviceSize _blockMinSize;
		vector<MemoryBlock> _blocks;
		VkDeviceSize _alignment;
		uint32_t _memoryType;
		MemoryType _allocatorType;
		uint64_t _idCounter;
		mutex _mutex;
	};

	class MemoryAllocatorManager
	{
	public:
		MemoryAllocatorManager(Device& device);
		~MemoryAllocatorManager();

		MemoryAllocator* GetMemoryAllocator(MemoryType memoryType) const;

		void Allocate(MemoryAllocation& outAllocation, MemoryType type, VkDeviceSize size, bool needDedicated);
		void Deallocate(MemoryAllocation& allocation);
		
		void BindBufferMemory(Buffer& buffer, MemoryAllocation& allocation);
		void BindImageMemory(Image& image, MemoryAllocation& allocation);
		void GetMappedPtr(void** outMappedPtr, MemoryAllocation& allocation);
		void MapMemory(void** outMappedPtr, MemoryAllocation& allocation);
		void UnmapMemory(MemoryAllocation& allocation);

		void CopyBuffer(void* srcData, MemoryAllocation& allocation, VkDeviceSize size);
	private:
		Device& _device;
		unordered_map<MemoryType, MemoryAllocator*> _memoryAllocators;
	};
}