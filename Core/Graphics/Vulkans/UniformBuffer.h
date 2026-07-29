#pragma once

namespace Core
{
	class Buffer;

	// Non-owning view over a Buffer, used to produce a uniform-buffer descriptor
	// write. The referenced Buffer is owned elsewhere and must outlive any
	// descriptor set this view was written into.
	class UniformBuffer
	{
	public:
		UniformBuffer() = default;

		void SetBuffer(Buffer* data);

		VkWriteDescriptorSet CreateWriteDescriptorSet(uint32_t binding);
	private:
		Buffer* _buffer = nullptr;
		VkDescriptorBufferInfo _bufferInfo{};
	};

	struct UniformBufferLayoutBinding
	{
	public:
		UniformBufferLayoutBinding(uint32_t binding, VkShaderStageFlags stage, VkDeviceSize bufferSize);

		uint32_t Binding;
		VkShaderStageFlags Stage;
		VkDeviceSize BufferSize;
	};
}
