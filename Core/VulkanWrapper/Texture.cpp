#include "stdafx.h"
#include "Texture.h"
#include "Image.h"
#include "Sampler.h"
#include "CommandBuffer.h"

Core::Texture::Texture(Device& device, string name, Image* image, Sampler* sampler)
	:_device(device), _name(name), _image(image), _sampler(sampler)
{
}

Core::Texture::~Texture()
{
	//TODO : Implement a resouce cache system.
	/*if (_image != nullptr)
		delete(_image);

	if (_sampler != nullptr)
		delete(_sampler);*/
}

VkDescriptorImageInfo Core::Texture::GetDescriptorImageInfo()
{
	VkDescriptorImageInfo imageInfo{};

	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfo.imageView = _image->GetImageView();
	imageInfo.sampler = _sampler->GetSampler();

	return imageInfo;
}