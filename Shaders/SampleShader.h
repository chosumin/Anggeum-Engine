#pragma once
#include "Shader.h"

namespace Core
{
	class SampleShader : public Shader
	{
	public:
		SampleShader(Device& device);

		virtual type_index GetType() override;
		virtual string GetPass() override;
		virtual bool UseInstancing() override;

		virtual vector<string> GetVertexAttirbuteNames() const override;

		virtual void Prepare() override;
	};
}

