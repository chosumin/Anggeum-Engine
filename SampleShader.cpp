#include "stdafx.h"
#include "SampleShader.h"
#include "TextureBuffer.h"
#include "Mesh.h"
#include "UniformBuffer.h"

namespace Core
{
	SampleShader::SampleShader(Device& device)
		:Shader(device,
			"shaders/simple_vs.vert.spv",
			"shaders/simple_fs.frag.spv")
	{
		auto cameraBuffer = new UniformBufferLayoutBinding(0);

		auto textureBuffer = new TextureBufferLayoutBinding(1);

		vector<PushConstant> pushConstants = 
		{ 
			{sizeof(ModelWorld), VK_SHADER_STAGE_VERTEX_BIT} 
		};

		CreatePipelineLayout({ cameraBuffer, textureBuffer }, pushConstants);

		delete(cameraBuffer);
		delete(textureBuffer);
	}

	SampleShader::~SampleShader()
	{
	}

	type_index SampleShader::GetType()
	{
		return typeid(SampleShader);
	}
}
