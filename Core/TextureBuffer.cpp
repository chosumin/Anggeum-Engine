#include "stdafx.h"
#include "TextureBuffer.h"

Core::TextureBuffer::TextureBuffer(uint32_t binding)
	:_binding(binding)
{
}

void Core::TextureBuffer::CopyDescriptorImageInfo(VkDescriptorImageInfo info)
{
	_imageInfo.imageLayout = info.imageLayout;
	_imageInfo.imageView = info.imageView;
	_imageInfo.sampler = info.sampler;
}

VkWriteDescriptorSet Core::TextureBuffer::CreateWriteDescriptorSet(size_t index)
{
	VkWriteDescriptorSet descriptorWrite{};
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstBinding = _binding;
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pImageInfo = &_imageInfo;

	return descriptorWrite;
}

Core::TextureBufferLayoutBinding::TextureBufferLayoutBinding(uint32_t binding)
	:_binding(binding)
{
}

VkDescriptorSetLayoutBinding Core::TextureBufferLayoutBinding::CreateDescriptorSetLayoutBinding()
{
	VkDescriptorSetLayoutBinding samplerLayoutBinding{};
	samplerLayoutBinding.binding = _binding;
	samplerLayoutBinding.descriptorCount = 1;
	samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerLayoutBinding.pImmutableSamplers = nullptr;
	samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	return samplerLayoutBinding;
}

VkDescriptorType Core::TextureBufferLayoutBinding::GetDescriptorType()
{
	return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
}
