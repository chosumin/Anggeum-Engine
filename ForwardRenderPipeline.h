#pragma once
#include "IRenderPipeline.h"

namespace Core
{
	class RenderPass;
	class Scene;
	class SwapChain;
	class ForwardRenderPipeline : public IRenderPipeline
	{
	public:
		ForwardRenderPipeline(Device& device, 
			Scene& scene, SwapChain& swapChain);
		virtual ~ForwardRenderPipeline() override;

		virtual void Prepare() override;
		virtual void Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex) override;
	private:
		void AddRenderPass(RenderPass* renderPass)
		{
			_renderPasses.push_back(renderPass);
		}
	private:
		vector<RenderPass*> _renderPasses;
	};
}

