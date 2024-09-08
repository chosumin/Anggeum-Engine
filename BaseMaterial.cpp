#include "stdafx.h"
#include "BaseMaterial.h"
using namespace Core;

BaseMaterial::BaseMaterial(Core::Device& device)
    :Material(device, "Sample")
{
	auto texture = new Texture(device,
		"Textures/white.png", Core::TextureFormat::Rgb_alpha);
	SetBuffer(1, texture);
}

type_index BaseMaterial::GetType() const
{
    return typeid(BaseMaterial);
}
