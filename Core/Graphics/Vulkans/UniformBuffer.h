#pragma once
#include "IDescriptor.h"

namespace Core
{
	class Buffer;
	class UniformBuffer
	{
	public:
		UniformBuffer(Device& device, VkDeviceSize bufferSize);
		~UniformBuffer();

		void SetBuffer(void* data);

		VkWriteDescriptorSet CreateWriteDescriptorSet(uint32_t binding);
	private:
		void CreateUniformBuffer(VkDeviceSize bufferSize);
	private:
		Device& _device;
		void* _uniformBufferMapped;
		unique_ptr<Buffer> _buffer;
		VkDescriptorBufferInfo _bufferInfo;
	};

	struct UniformBufferLayoutBinding : public IDescriptor
	{
	public:
		UniformBufferLayoutBinding(uint32_t binding, VkShaderStageFlags stage, VkDeviceSize bufferSize);

		VkDescriptorSetLayoutBinding CreateDescriptorSetLayoutBinding();
		VkDescriptorType GetDescriptorType();
	
		uint32_t Binding;
		VkShaderStageFlags Stage;
		VkDeviceSize BufferSize;
	};
}

