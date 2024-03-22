#pragma once
#include <cstdint>

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
		virtual RenderPass& GetRenderPass(uint32_t index) = 0;
	};
}

