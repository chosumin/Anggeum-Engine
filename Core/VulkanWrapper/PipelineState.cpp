#include "stdafx.h"
#include "PipelineState.h"

namespace Core
{
	PipelineState::PipelineState()
	{
		InitRasterizationStateCreateInfo();
		InitMultisampleStateCreateInfo();
	}
	
	void PipelineState::InitRasterizationStateCreateInfo()
	{
		_rasterizationStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		_rasterizationStateCreateInfo.rasterizerDiscardEnable = VK_FALSE;
		_rasterizationStateCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
		_rasterizationStateCreateInfo.lineWidth = 1.0f;
		_rasterizationStateCreateInfo.cullMode = VK_CULL_MODE_BACK_BIT;
		_rasterizationStateCreateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

		_rasterizationStateCreateInfo.depthClampEnable = VK_FALSE;
		_rasterizationStateCreateInfo.depthBiasEnable = VK_FALSE;
		_rasterizationStateCreateInfo.depthBiasConstantFactor = 0.0f;
		_rasterizationStateCreateInfo.depthBiasClamp = 0.0f;
		_rasterizationStateCreateInfo.depthBiasSlopeFactor = 0.0f;
	}

	void PipelineState::InitMultisampleStateCreateInfo()
	{
		_multisampleStateCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		_multisampleStateCreateInfo.sampleShadingEnable = VK_TRUE;
		_multisampleStateCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		_multisampleStateCreateInfo.minSampleShading = 0.2f;
		_multisampleStateCreateInfo.pSampleMask = nullptr; // Optional
		_multisampleStateCreateInfo.alphaToCoverageEnable = VK_FALSE; // Optional
		_multisampleStateCreateInfo.alphaToOneEnable = VK_FALSE; // Optional
	}
}
