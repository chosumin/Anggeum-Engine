#include "stdafx.h"
#include "ShadowMaterial.h"
using namespace Core;

ShadowMaterial::ShadowMaterial(Core::Device& device)
	:Material(device, "Shadow")
{
}

type_index ShadowMaterial::GetType() const
{
	return typeid(ShadowMaterial);
}
