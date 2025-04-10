#pragma once
#include "VulkanWrapper/Shader.h"

namespace Core
{
	class IrradianceShader : public Shader
	{
	public:
		IrradianceShader(Device& device);

		virtual type_index GetType() override;
		virtual string GetPass() override;
		virtual bool UseInstancing() override;

		virtual vector<string> GetVertexAttirbuteNames() const override;

		virtual void Prepare() override;
	};
}

