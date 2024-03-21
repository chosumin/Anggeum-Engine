#pragma once
#include "Shader.h"

namespace Core
{
	class CommandBuffer;
	class SampleShader : public Shader
	{
	public:
		SampleShader(Device& device);
		virtual ~SampleShader() override;

		virtual type_index GetType() override;
		virtual string GetPass() override;
		virtual bool UseInstancing() override;

		virtual VkPipelineVertexInputStateCreateInfo GetVertexInputStateCreateInfo() override;

	};
}

