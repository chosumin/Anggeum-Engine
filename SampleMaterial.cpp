#include "stdafx.h"
#include "SampleMaterial.h"
using namespace Core;

SampleMaterial::SampleMaterial(Core::Device& device)
	:Material(device, "Sample")
{
	auto texture = new Texture(device,
		"Textures/viking_room.png", Core::TextureFormat::Rgb_alpha);
	SetBuffer(1, texture);
}

type_index SampleMaterial::GetType() const
{
    return typeid(SampleMaterial);
}
