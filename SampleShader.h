#pragma once
#include "Shader.h"

namespace Core
{
	class Texture;
	class CommandBuffer;
	class SampleShader : public Shader
	{
	public:
		SampleShader(Device& device, CommandBuffer& commandBuffer);
		virtual ~SampleShader() override;

		virtual type_index GetType() override;

	private:
		Texture* _texture;
	};
}

