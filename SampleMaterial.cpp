#include "stdafx.h"
#include "SampleMaterial.h"
#include "ShaderFactory.h"
#include "PerspectiveCamera.h"
using namespace Core;

SampleMaterial::SampleMaterial(Core::Device& device)
	:Material(device)
{
	_shader = ShaderFactory::CreateShader(device, "Sample");

	//hack : binding duplication declaration.
	auto cameraBuffer = new UniformBuffer(device, sizeof(VPBufferObject), 0);
	_uniformBuffers.insert({ 0, cameraBuffer });

	//hack : binding duplication declaration.
	auto textureBuffer = new TextureBuffer(1);
	_textureBuffers.insert({ 1, textureBuffer });

	auto texture = new Texture(device,
		"Textures/viking_room.png", Core::TextureFormat::Rgb_alpha, 1);
	SetBuffer(1, texture);

	UpdateDescriptorSets();
}

type_index SampleMaterial::GetType() const
{
    return typeid(SampleMaterial);
}
