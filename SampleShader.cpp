#include "stdafx.h"
#include "SampleShader.h"
#include "CameraUniformBuffer.h"

namespace Core
{
	SampleShader::SampleShader(Device& device,
		//vector<IDescriptor*> descriptors,
		vector<IPushConstant*> pushConstants)
		:Shader(device,
			"shaders/simple_vs.vert.spv",
			"shaders/simple_fs.frag.spv")
	{
		auto cameraBuffer = 
			new CameraUniformBuffer(sizeof(VPBufferObject), 0);

		CreateDescriptors(descriptors);
		CreatePushConstants(pushConstants);

		//todo : 레이아웃 새로 생성
		CreatePipelineLayout();
	}

	SampleShader::~SampleShader()
	{
	}

	type_index SampleShader::GetType()
	{
		return typeid(SampleShader);
	}
}
