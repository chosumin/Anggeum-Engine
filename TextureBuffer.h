#pragma once
#include "IDescriptor.h"

namespace Core
{
	class TextureBuffer : public IDescriptor
	{
	public:
		TextureBuffer(uint32_t binding);

		void SetDescriptorImageInfo(VkDescriptorImageInfo& info);
		VkDescriptorSetLayoutBinding CreateDescriptorSetLayoutBinding();
		VkWriteDescriptorSet CreateWriteDescriptorSet(size_t index);
		VkDescriptorType GetDescriptorType();
	private:
		uint32_t _binding;
		VkDescriptorImageInfo _imageInfo;
	};
}

