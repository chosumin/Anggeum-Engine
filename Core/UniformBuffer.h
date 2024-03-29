#pragma once
#include "IDescriptor.h"

namespace Core
{
	class Buffer;
	class UniformBuffer
	{
	public:
		UniformBuffer(Device& device, VkDeviceSize bufferSize, uint32_t binding);
		~UniformBuffer();

		void SetBuffer(uint32_t currentImage, void* data);

		VkWriteDescriptorSet CreateWriteDescriptorSet(size_t index);
	private:
		void CreateUniformBuffer();
	private:
		Device& _device;
		VkDeviceSize _bufferSize;
		vector<void*> _uniformBuffersMapped;
		vector<unique_ptr<Buffer>> _buffers;
		VkDescriptorBufferInfo _bufferInfo;
		uint32_t _binding;
	};

	class UniformBufferLayoutBinding : public IDescriptor
	{
	public:
		UniformBufferLayoutBinding(uint32_t binding);

		VkDescriptorSetLayoutBinding CreateDescriptorSetLayoutBinding();
		VkDescriptorType GetDescriptorType();
	private:
		uint32_t _binding;
	};
}

