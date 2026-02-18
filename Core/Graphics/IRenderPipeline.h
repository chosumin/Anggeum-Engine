#pragma once
#include "Graphics/RenderFrame.h"
#include "Graphics/Vulkans/Texture.h"

namespace Core
{
	class RenderContext;
	class IRenderPipeline
	{
	public:
		virtual ~IRenderPipeline() = default;
		
		virtual void Prepare() = 0;
		virtual void Draw(RenderFrame& renderFrame, uint32_t imageIndex) = 0;
		virtual VkSampleCountFlagBits GetMSAASamples() const = 0;
	};
}

