#pragma once
#include "Graphics/Vulkans/PipelineState.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/RenderPass.h"
#include "Graphics/Vulkans/Framebuffer.h"

namespace Core
{
	class SwapChain;
	class RenderPass;
	class Framebuffer;
	class PipelineState;
	class CommandBuffer;
	class RendererPass
	{
	public:
		RendererPass(Device& device);
		virtual ~RendererPass();

		virtual void Prepare() = 0;
		virtual void Draw(CommandBuffer& commandBuffer,
			uint32_t currentFrame, uint32_t imageIndex) = 0;
	protected:
		void CreateFrameBuffer(SwapChain& swapChain);
		void CreateFrameBuffer(Image* image);
	protected:
		Device& _device;
		RenderPass* _renderPass;
		Framebuffer* _framebuffer;
		PipelineState* _pipelineState;
	};
}