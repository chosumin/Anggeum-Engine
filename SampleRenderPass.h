#pragma once
#include "RenderPass.h"

namespace Core
{
	class SampleRenderPass : public RenderPass
	{
	public:
		SampleRenderPass(Device& device, VkExtent2D extent, VkFormat colorFormat);
		virtual ~SampleRenderPass() override;
	};
}

