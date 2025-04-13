#pragma once
#include <cstdint>
#include "Graphics/Vulkans/Texture.h"

namespace Core
{
	class CommandBuffer;
	class RenderPass;
	class IRenderPipeline
	{
	public:
		virtual ~IRenderPipeline() {}
		virtual void Prepare() = 0;
		virtual void Draw(CommandBuffer& commandBuffer,
			uint32_t currentFrame, uint32_t imageIndex) = 0;
		virtual Texture* GetColorRenderTarget() = 0;
		virtual VkSampleCountFlagBits GetMSAASamples() const = 0;
	};
}

