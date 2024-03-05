#include "stdafx.h"
#include "GeometryRenderPass.h"

namespace Core
{
	GeometryRenderPass::GeometryRenderPass(Device& device, VkExtent2D extent, VkFormat colorFormat)
		:RenderPass(device)
	{
		CreateColorRenderTarget(extent, colorFormat);
		CreateDepthRenderTarget(extent);
		CreateRenderPass();
	}

	GeometryRenderPass::~GeometryRenderPass()
	{
	}
}
