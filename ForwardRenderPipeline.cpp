#include "stdafx.h"
#include "ForwardRenderPipeline.h"
#include "RenderPass.h"
#include "GeometryRenderPass.h"
#include "Scene.h"
#include "SwapChain.h"
using namespace Core;

Core::ForwardRenderPipeline::ForwardRenderPipeline(
	Device& device, Scene& scene, SwapChain& swapChain)
{
	auto geometryRenderPass = new GeometryRenderPass(
		device, scene, swapChain);
	AddRenderPass(geometryRenderPass);
}

Core::ForwardRenderPipeline::~ForwardRenderPipeline()
{
	for (auto&& renderPass : _renderPasses)
	{
		delete(renderPass);
	}
}

void ForwardRenderPipeline::Prepare()
{
	for (auto&& renderPass : _renderPasses)
	{
		renderPass->Prepare();
	}
}

void ForwardRenderPipeline::Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex)
{
	for (auto&& renderPass : _renderPasses)
	{
		renderPass->Draw(commandBuffer, currentFrame, imageIndex);
	}
}

RenderPass& Core::ForwardRenderPipeline::GetRenderPass(uint32_t index)
{
	return *_renderPasses[index];
}
