#pragma once

namespace Core
{
	class Buffer;

	// Non-owning view over a Buffer, used to produce a storage-buffer descriptor
	// write. The referenced Buffer is owned elsewhere and must outlive any
	// descriptor set this view was written into.
	class StorageBuffer
	{
	public:
		StorageBuffer() = default;

		void SetBuffer(Buffer* data);

		VkWriteDescriptorSet CreateWriteDescriptorSet(uint32_t binding);
	private:
		Buffer* _buffer = nullptr;
		VkDescriptorBufferInfo _bufferInfo{};
	};

	struct StorageBufferLayoutBinding
	{
	public:
		StorageBufferLayoutBinding(uint32_t binding, VkShaderStageFlags stage);

		uint32_t Binding;
		VkShaderStageFlags Stage;
	};
}