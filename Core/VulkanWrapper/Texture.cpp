#include "stdafx.h"
#include "Texture.h"
#include "CommandBuffer.h"

Core::Texture::Texture(string name, Image* image, Sampler* sampler)
	: _name(name), _image(image), _sampler(sampler)
{
}

Core::Texture::~Texture()
{
	//TODO : Implement a resouce cache system.
	//Cleanup();
}

void Core::Texture::Cleanup()
{
	if (_image != nullptr)
		delete(_image);
}

VkDescriptorImageInfo Core::Texture::GetDescriptorImageInfo()
{
	VkDescriptorImageInfo imageInfo{};

	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfo.imageView = _image->GetImageView();
	imageInfo.sampler = _sampler->GetSampler();

	return imageInfo;
}

uint32_t Core::Texture::GetMipLevels() const
{
	return _image->_mipLevels;
}

uint32_t Core::Texture::GetLayers() const
{
	return _image->_layer;
}
