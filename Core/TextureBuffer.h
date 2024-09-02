#pragma once
#include "IDescriptor.h"

namespace Core
{
	class TextureBuffer
	{
	public:
		TextureBuffer(uint32_t binding);

		void CopyDescriptorImageInfo(VkDescriptorImageInfo info);
		
		VkWriteDescriptorSet CreateWriteDescriptorSet(size_t index);
	private:
		uint32_t _binding;
		VkDescriptorImageInfo _imageInfo;
	};

	class TextureBufferLayoutBinding : public IDescriptor
	{
	public:
		TextureBufferLayoutBinding(uint32_t binding);

		VkDescriptorSetLayoutBinding CreateDescriptorSetLayoutBinding();
		VkDescriptorType GetDescriptorType();

		uint32_t GetBinding() { return _binding; }
	private:
		uint32_t _binding;
	};
}

