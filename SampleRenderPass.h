#pragma once
#include "RenderPass.h"

namespace Core
{
	class SampleRenderPass : public RenderPass
	{
	public:
		SampleRenderPass(Device& device);
		virtual ~SampleRenderPass() override;
	};
}

