#include "stdafx.h"
#include "Pipeline.h"
#include "Shader.h"
#include "PipelineState.h"

Core::Pipeline::Pipeline(Device& device,
	const PipelineRenderingDesc& renderingDesc, Shader& shader, PipelineState& pipelineState)
	:_device(device), _pipelineBindPoint(VK_PIPELINE_BIND_POINT_GRAPHICS)
{
	VkPipelineRenderingCreateInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = static_cast<uint32_t>(renderingDesc.colorFormats.size());
	renderingInfo.pColorAttachmentFormats = renderingDesc.colorFormats.data();
	renderingInfo.depthAttachmentFormat = renderingDesc.depthFormat;
	renderingInfo.stencilAttachmentFormat = renderingDesc.stencilFormat;

	CreateGraphicsPipeline(VK_NULL_HANDLE, &renderingInfo, shader, pipelineState);
}

Core::Pipeline::Pipeline(Device& device, Shader& shader)
	:_device(device), _pipelineBindPoint(VK_PIPELINE_BIND_POINT_COMPUTE)
{
	VkComputePipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.layout = shader.GetPipelineLayout();
	pipelineInfo.stage = shader.GetComputeShaderStageCreateInfo();

	if (vkCreateComputePipelines(device.GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_pipeline) != VK_SUCCESS) {
		throw std::runtime_error("failed to create compute pipeline!");
	}
}

Core::Pipeline::~Pipeline()
{
	auto device = _device.GetDevice();
	vkDestroyPipeline(device, _pipeline, nullptr);
}

void Core::Pipeline::CreateGraphicsPipeline(VkRenderPass renderPass,
	const VkPipelineRenderingCreateInfo* renderingInfo,
	Shader& shader, PipelineState& pipelineState)
{
	auto shaderStage = shader.GetShaderStageCreateInfo();
	auto vertexInputState =
		shader.GetVertexInputStateCreateInfo();
	auto inputAssemblyState =
		pipelineState.GetInputAssemblyStateCreateInfo();
	auto viewportState = GetViewportStateCreateInfo();

	auto depthStencilState = pipelineState.GetDepthStencilStateCreateInfo();

	const uint32_t colorAttachmentCount =
		renderingInfo != nullptr ? renderingInfo->colorAttachmentCount : 1;
	vector<VkPipelineColorBlendAttachmentState> blendAttachments(
		colorAttachmentCount, GetColorBlendAttachmentState());
	auto colorBlendState = GetColorBlendStateCreateInfo(blendAttachments);

	auto dynamicState = GetDynamicStateCreateInfo();
	auto pipelineLayout = shader.GetPipelineLayout();

	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = renderingInfo;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStage.data();
	pipelineInfo.pVertexInputState = &vertexInputState;
	pipelineInfo.pInputAssemblyState = &inputAssemblyState;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &pipelineState.GetRasterizationStateCreateInfo();
	pipelineInfo.pMultisampleState = &pipelineState.GetMultisampleStateCreateInfo();
	pipelineInfo.pDepthStencilState = &depthStencilState;
	pipelineInfo.pColorBlendState = &colorBlendState;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = pipelineLayout;
	pipelineInfo.renderPass = renderPass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineInfo.basePipelineIndex = -1;

	if (vkCreateGraphicsPipelines(_device.GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_pipeline) != VK_SUCCESS)
		throw std::runtime_error("failed to create graphics pipeline!");
}

VkPipelineViewportStateCreateInfo Core::Pipeline::GetViewportStateCreateInfo()
{
	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	return viewportState;
}

VkPipelineColorBlendStateCreateInfo Core::Pipeline::GetColorBlendStateCreateInfo(
	const vector<VkPipelineColorBlendAttachmentState>& blendAttachments)
{
	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
	colorBlending.pAttachments = blendAttachments.data();
	colorBlending.blendConstants[0] = 0.0f;
	colorBlending.blendConstants[1] = 0.0f;
	colorBlending.blendConstants[2] = 0.0f;
	colorBlending.blendConstants[3] = 0.0f;

	return colorBlending;
}

VkPipelineDynamicStateCreateInfo Core::Pipeline::GetDynamicStateCreateInfo()
{
	VkPipelineDynamicStateCreateInfo dynamicState{};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = static_cast<uint32_t>(_dynamicStates.size());
	dynamicState.pDynamicStates = _dynamicStates.data();

	return dynamicState;
}

VkPipelineColorBlendAttachmentState Core::Pipeline::GetColorBlendAttachmentState()
{
	VkPipelineColorBlendAttachmentState colorBlendAttachment{};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

	return colorBlendAttachment;
}