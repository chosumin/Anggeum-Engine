#pragma once
#include "Shader.h"

class ShadowShader : public Core::Shader
{
public:
	ShadowShader(Core::Device& device);

	virtual type_index GetType() override;
	virtual string GetPass() override;
	virtual bool UseInstancing() override;

	virtual vector<string> GetVertexAttirbuteNames() const override;

	virtual void Prepare() override;
};

