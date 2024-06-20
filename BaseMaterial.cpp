#include "stdafx.h"
#include "BaseMaterial.h"
#include "ShaderFactory.h"
using namespace Core;

BaseMaterial::BaseMaterial(Core::Device& device)
    :Material(device, "Sample")
{
	//todo : add buffer automatically based on the shader properties.
	
	//hack : binding duplication declaration.
	auto cameraBuffer = new UniformBuffer(device, sizeof(VPBufferObject), 0);
	_uniformBuffers.insert({ 0, cameraBuffer });

	//hack : binding duplication declaration.
	auto textureBuffer = new TextureBuffer(1);
	_textureBuffers.insert({ 1, textureBuffer });

	auto texture = new Texture(device,
		"Textures/white.png", Core::TextureFormat::Rgb_alpha, 1);
	SetBuffer(1, texture);
}

type_index BaseMaterial::GetType() const
{
    return typeid(BaseMaterial);
}
