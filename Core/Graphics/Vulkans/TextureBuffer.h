#pragma once
#include "IDescriptor.h"

namespace Core
{
	class TextureBuffer
	{
	public:
		TextureBuffer();

		void CopyDescriptorImageInfo(VkDescriptorImageInfo info);
		
		VkWriteDescriptorSet CreateWriteDescriptorSet(size_t index, uint32_t binding);
	private:
		VkDescriptorImageInfo _imageInfo;
	};

	struct TextureBufferLayoutBinding : public IDescriptor
	{
	public:
		TextureBufferLayoutBinding(uint32_t binding, VkShaderStageFlags stage);

		VkDescriptorSetLayoutBinding CreateDescriptorSetLayoutBinding();
		VkDescriptorType GetDescriptorType();

		uint32_t Binding;
		VkShaderStageFlags Stage;
	};
}

