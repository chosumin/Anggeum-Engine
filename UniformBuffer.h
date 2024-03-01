#pragma once
#include "IDescriptor.h"

namespace Core
{
	class Buffer;
	class UniformBuffer : public IDescriptor
	{
	public:
		UniformBuffer(VkDeviceSize bufferSize, uint32_t binding);
		~UniformBuffer();

		void SetBuffer(uint32_t currentImage, void* data);

		VkDescriptorSetLayoutBinding CreateDescriptorSetLayoutBinding();
		VkWriteDescriptorSet CreateWriteDescriptorSet(size_t index);
		VkDescriptorType GetDescriptorType();
	private:
		void CreateUniformBuffer();
	private:
		VkDeviceSize _bufferSize;
		vector<void*> _uniformBuffersMapped;
		vector<unique_ptr<Buffer>> _buffers;
		VkDescriptorBufferInfo _bufferInfo;
		uint32_t _binding;
	};
}

