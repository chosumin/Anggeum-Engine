#include "stdafx.h"
#include "SampleMaterial.h"
#include "ShaderFactory.h"
using namespace Core;

SampleMaterial::SampleMaterial(Core::Device& device)
	:Material(device, "Sample")
{
	//hack : binding duplication declaration.
	auto cameraBuffer = new UniformBuffer(device, sizeof(VPBufferObject), 0);
	_uniformBuffers.insert({ 0, cameraBuffer });

	//hack : binding duplication declaration.
	auto textureBuffer = new TextureBuffer(1);
	_textureBuffers.insert({ 1, textureBuffer });

	auto texture = new Texture(device,
		"Textures/viking_room.png", Core::TextureFormat::Rgb_alpha);
	SetBuffer(1, texture);

	_textureBuffers.insert({ 2, new TextureBuffer(2) });

	auto shadowBuffer = new UniformBuffer(device, sizeof(ShadowUniform), 3);
	_uniformBuffers.insert({ 3, shadowBuffer });
}

type_index SampleMaterial::GetType() const
{
    return typeid(SampleMaterial);
}
