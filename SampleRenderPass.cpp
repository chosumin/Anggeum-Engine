#include "stdafx.h"
#include "SampleRenderPass.h"

Core::SampleRenderPass::SampleRenderPass(Device& device, VkExtent2D extent, VkFormat colorFormat)
	:RenderPass(device)
{
	CreateColorRenderTarget(extent, colorFormat);
	CreateDepthRenderTarget(extent);
	CreateRenderPass();
}

Core::SampleRenderPass::~SampleRenderPass()
{
}
