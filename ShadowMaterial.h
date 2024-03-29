#pragma once
#include "Material.h"

class ShadowMaterial : public Core::Material
{
public:
	ShadowMaterial(Core::Device& device);
	virtual type_index GetType() const override;
};

