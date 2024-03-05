#pragma once
#include "RenderPass.h"

namespace Core
{
	class GeometryRenderPass : public RenderPass
	{
	public:
		GeometryRenderPass(Device& device, VkExtent2D extent, VkFormat colorFormat);
		virtual ~GeometryRenderPass() override;
	};
}

