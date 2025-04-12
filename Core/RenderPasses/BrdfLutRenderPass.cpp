#include "stdafx.h"
#include "BrdfLutRenderPass.h"
#include "VulkanWrapper/CommandBuffer.h"
#include "VulkanWrapper/Pipeline.h"
#include "VulkanWrapper/Texture.h"
#include "VulkanWrapper/Shader.h"
#include "Utils/Utility.h"
#include "Material.h"
using namespace Core;

Core::BrdfLutRenderPass::BrdfLutRenderPass(Device& device, Texture* colorBuffer)
	:RenderPass(device), _colorRenderTarget(colorBuffer)
{
	CreateColorAttachment(colorBuffer, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	CreateRenderPass();
	CreateFrameBuffer(colorBuffer->GetImage());
}

Core::BrdfLutRenderPass::~BrdfLutRenderPass()
{
	delete(_pipeline);
	delete(_material);
}

void Core::BrdfLutRenderPass::Prepare()
{
	uint32_t hash = Utility::HashCode("BRDF");
	_material = new Material(_device, "BRDF", hash);

	auto pipelineState = *_pipelineState;

	auto& rasterization = pipelineState.GetRasterizationStateCreateInfo();
	rasterization.cullMode = VK_CULL_MODE_NONE;

	auto& depthInfo = pipelineState.GetDepthStencilStateCreateInfo();
	depthInfo.depthWriteEnable = VK_FALSE;
	depthInfo.depthTestEnable = VK_FALSE;

	_pipeline = new Pipeline(_device, *this, _material->GetShader(), pipelineState);
}

void Core::BrdfLutRenderPass::Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex)
{
	commandBuffer.SetViewportAndScissor(GetBufferExtent2D());

	commandBuffer.BeginRenderPass(CreateRenderPassBeginInfo(imageIndex));

	commandBuffer.BindPipeline(_pipeline);

	commandBuffer.Draw(3, 1);

	commandBuffer.EndRenderPass();
}