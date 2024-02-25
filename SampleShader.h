#pragma once
#include "Shader.h"

namespace Core
{
	class SampleShader : public Shader
	{
	public:
		SampleShader(Device& device,
			//vector<IDescriptor*> descriptors,
			vector<IPushConstant*> pushConstants);
		virtual ~SampleShader() override;

		virtual type_index GetType() override;
	};
}

