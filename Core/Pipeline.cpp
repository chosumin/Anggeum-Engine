#include "stdafx.h"
#include "Pipeline.h"
#include "RenderPass.h"
#include "Vertex.h"
#include "Shader.h"
#include "PipelineState.h"

Core::Pipeline::Pipeline(Device& device, 
	RenderPass& renderPass, Shader& shader, PipelineState& pipelineState)
	:_device(device)
{
	auto shaderStage = shader.GetShaderStageCreateInfo();
	auto vertexInputState =
		shader.GetVertexInputStateCreateInfo();
	auto inputAssemblyState =
		GetInputAssemblyStateCreateInfo();
	auto viewportState = GetViewportStateCreateInfo();

	auto depthStencilState = GetDepthStencilStateCreateInfo();
	auto colorBlendAttachment = GetColorBlendAttachmentState();
	auto colorBlendState = GetColorBlendStateCreateInfo(colorBlendAttachment);
	auto dynamicState = GetDynamicStateCreateInfo();
	auto pipelineLayout = shader.GetPipelineLayout();

	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
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
	pipelineInfo.renderPass = renderPass.GetHandle();
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
	pipelineInfo.basePipelineIndex = -1; // Optional

	if (vkCreateGraphicsPipelines(device.GetDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_graphicsPipeline) != VK_SUCCESS)
		throw std::runtime_error("failed to create graphics pipeline!");
}

Core::Pipeline::~Pipeline()
{
	auto device = _device.GetDevice();
	vkDestroyPipeline(device, _graphicsPipeline, nullptr);
}

VkPipelineInputAssemblyStateCreateInfo Core::Pipeline::GetInputAssemblyStateCreateInfo()
{
	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssembly.primitiveRestartEnable = VK_FALSE;

	return inputAssembly;
}

VkPipelineViewportStateCreateInfo Core::Pipeline::GetViewportStateCreateInfo()
{
	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	return viewportState;
}

VkPipelineDepthStencilStateCreateInfo Core::Pipeline::GetDepthStencilStateCreateInfo()
{
	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
	depthStencil.depthBoundsTestEnable = VK_FALSE;
	depthStencil.stencilTestEnable = VK_FALSE;

	return depthStencil;
}

VkPipelineColorBlendStateCreateInfo Core::Pipeline::GetColorBlendStateCreateInfo(
	VkPipelineColorBlendAttachmentState& colorBlendAttachment)
{
	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorBlendAttachment;
	colorBlending.blendConstants[0] = 0.0f; // Optional
	colorBlending.blendConstants[1] = 0.0f; // Optional
	colorBlending.blendConstants[2] = 0.0f; // Optional
	colorBlending.blendConstants[3] = 0.0f; // Optional

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
	//Pseudocode for color blend operation below.
	//if (blendEnable) {
	//	finalColor.rgb = (srcColorBlendFactor * newColor.rgb) < colorBlendOp > (dstColorBlendFactor * oldColor.rgb);
	//	finalColor.a = (srcAlphaBlendFactor * newColor.a) < alphaBlendOp > (dstAlphaBlendFactor * oldColor.a);
	//}
	//else {
	//	finalColor = newColor;
	//}
	//finalColor = finalColor & colorWriteMask;

	VkPipelineColorBlendAttachmentState colorBlendAttachment{};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

	return colorBlendAttachment;
}