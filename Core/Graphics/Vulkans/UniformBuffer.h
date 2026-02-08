#pragma once

namespace Core
{
	class Buffer;
	class UniformBuffer
	{
	public:
		UniformBuffer(Device& device, VkDeviceSize bufferSize);
		~UniformBuffer();

		void Update(void* data);
		void Update(void* data, VkDeviceSize offset, VkDeviceSize size);

		VkWriteDescriptorSet CreateWriteDescriptorSet(uint32_t binding);
	private:
		void CreateUniformBuffer(VkDeviceSize bufferSize);
	private:
		Device& _device;
		void* _uniformBufferMapped;
		unique_ptr<Buffer> _buffer;
		VkDescriptorBufferInfo _bufferInfo;
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

