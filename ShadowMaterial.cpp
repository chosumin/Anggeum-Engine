#include "stdafx.h"
#include "ShadowMaterial.h"
#include "ShaderFactory.h"
#include "PerspectiveCamera.h"
using namespace Core;

ShadowMaterial::ShadowMaterial(Core::Device& device)
	:Material(device)
{
	_shader = ShaderFactory::CreateShader(device, "Shadow");

	auto cameraBuffer = new UniformBuffer(device, sizeof(VPBufferObject), 0);
	_uniformBuffers.insert({ 0, cameraBuffer });

	UpdateDescriptorSets();
}

type_index ShadowMaterial::GetType() const
{
	return typeid(ShadowMaterial);
}
