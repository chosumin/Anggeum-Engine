#pragma once
#include "Material.h"

class BaseMaterial : public Core::Material
{
public:
	BaseMaterial(Core::Device& device);
	virtual type_index GetType() const override;
};

