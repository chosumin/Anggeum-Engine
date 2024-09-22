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

		void SetBuffer(uint32_t currentImage, void* data);

		VkWriteDescriptorSet CreateWriteDescriptorSet(size_t index, uint32_t binding);
	private:
		void CreateUniformBuffer(VkDeviceSize bufferSize);
	private:
		Device& _device;
		vector<void*> _uniformBuffersMapped;
		vector<unique_ptr<Buffer>> _buffers;
		VkDescriptorBufferInfo _bufferInfo;
	};

	struct UniformBufferLayoutBinding : public IDescriptor
	{
	public:
		UniformBufferLayoutBinding(uint32_t binding, VkShaderStageFlagBits stage, VkDeviceSize bufferSize);

		VkDescriptorSetLayoutBinding CreateDescriptorSetLayoutBinding();
		VkDescriptorType GetDescriptorType();
	
		uint32_t Binding;
		VkShaderStageFlagBits Stage;
		VkDeviceSize BufferSize;
	};
}

