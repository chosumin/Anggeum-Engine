#pragma once
#include "Material.h"

class SampleMaterial : public Core::Material
{
public:
	SampleMaterial(Core::Device& device);
	virtual type_index GetType() const override;
};

