#pragma once
#include "VulkanWrapper/Shader.h"

namespace Core
{
	class PrefilterShader : public Shader
	{
	public:
		PrefilterShader(Device& device);

		virtual type_index GetType() override;
		virtual string GetPass() override;
		virtual bool UseInstancing() override;

		virtual vector<string> GetVertexAttirbuteNames() const override;

		virtual void Prepare() override;
	};
}

