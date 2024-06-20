#include "stdafx.h"
#include "ShadowMaterial.h"
#include "ShaderFactory.h"
using namespace Core;

ShadowMaterial::ShadowMaterial(Core::Device& device)
	:Material(device, "Shadow")
{
	auto cameraBuffer = new UniformBuffer(device, sizeof(VPBufferObject), 0);
	_uniformBuffers.insert({ 0, cameraBuffer });
}

type_index ShadowMaterial::GetType() const
{
	return typeid(ShadowMaterial);
}
