#include "stdafx.h"
#include "Texture.h"
#include "Graphics/ResourceCache.h"

Core::Texture::Texture(string name, shared_ptr<Image> image, shared_ptr<Sampler> sampler)
	:_name(name), _image(image), _sampler(sampler)
{
}

Core::Texture::~Texture()
{
}

uint32_t Core::Texture::GetMipLevels() const
{
	return _image->_mipLevels;
}

uint32_t Core::Texture::GetLayers() const
{
	return _image->_layer;
}
