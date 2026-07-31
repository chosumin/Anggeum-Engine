#include "stdafx.h"
#include "Texture.h"
#include "Graphics/ResourceManager.h"

Core::Texture::Texture(string name, unique_ptr<Image> image, Handle<Sampler> sampler)
	:_name(name), _image(std::move(image)), _sampler(sampler)
{
}

Core::Texture::~Texture()
{
}

VkSampler Core::Texture::GetVkSampler()
{
	return _sampler.Get().GetSampler();
}

uint32_t Core::Texture::GetMipLevels() const
{
	return _image->_mipLevels;
}

uint32_t Core::Texture::GetLayers() const
{
	return _image->_layer;
}

VkWriteDescriptorSet Core::TextureBuffer::CreateWriteDescriptorSet(uint32_t binding, VkDescriptorType descriptorType)
{
	imageInfo.imageLayout = imageLayout;
	auto& tex = rawTexture != nullptr ? *rawTexture : texture.Get();
	imageInfo.imageView = tex.GetImage().GetOrCreateImageView(mipLevel);

	switch (descriptorType)
	{
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			imageInfo.sampler = VK_NULL_HANDLE;
			break;
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			imageInfo.sampler = tex.GetVkSampler();
			break;
		default:
			imageInfo.sampler = VK_NULL_HANDLE;
			break;
	}

	VkWriteDescriptorSet descriptorWrite{};
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstBinding = binding;
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = descriptorType;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pImageInfo = &imageInfo;

	return descriptorWrite;
}
