#pragma once
#include "MemoryAllocator.h"

namespace Core
{
	struct MemoryAllocation;
	class CommandBuffer;
	class CommandPool;

	// Buffer creation parameters, shared by the ResourceCache global buffer pool
	// and the FrameResources per-frame pool.
	struct BufferDesc
	{
		VkDeviceSize size = 0;
		VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		MemoryType memoryType = MemoryType::DEVICE_LOCAL;
	};

	class Buffer
	{
	public:
		Buffer(Device& device, VkDeviceSize size, VkBufferUsageFlags usage, MemoryType memoryType);
		~Buffer();

		VkBuffer GetBuffer() { return _buffer; }

		void CopyBuffer(void* data, VkDeviceSize size);
		void GetMappedPtr(void** data);
		void Map(void** data);
		void Unmap();

		// Upload `data` into the persistent mapping.
		template<typename T>
		void Update(const T& data)
		{
			static_assert(std::is_trivially_copyable<T>::value,
				"Uniform data must be trivially copyable");
			static_assert(!std::is_pointer<T>::value,
				"Pass the value, not a pointer to it");

			UpdateRaw(&data, sizeof(T));
		}

		VkDeviceSize GetSize() const { return _size; }
	private:
		// The byte count only matters here, where memcpy needs one; callers always
		// get it from the type they hand over.
		void UpdateRaw(const void* data, VkDeviceSize size);

		void* GetPersistentMappedPtr();

	protected:
		Device& _device;
		VkBuffer _buffer;
		VkDeviceSize _size;
		unique_ptr<MemoryAllocation> _allocation;
		MemoryAllocatorManager* _allocator;

		// Resolved on first Update() call; the allocator's block mapping is stable
		// for the buffer's lifetime.
		void* _mapped = nullptr;
	};
}

