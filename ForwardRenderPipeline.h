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
		virtual RenderPass& GetRenderPass(uint32_t index) override;

		void Cleanup();
		void Resize(SwapChain& swapChain);

		virtual VkSampleCountFlagBits GetMSAASamples() const override 
		{ 
			return _msaaSamples; 
		}
		virtual RenderTarget* GetColorRenderTarget() override 
		{ 
			return _renderTargets[0].get(); 
		}
	private:
		VkSampleCountFlagBits GetMaxUsableSampleCount();

		void AddRenderPass(RenderPass* renderPass)
		{
			_renderPasses.push_back(renderPass);
		}

		unique_ptr<RenderTarget> CreateRenderTarget(VkExtent2D extent, VkFormat format,
			VkImageLayout layout, VkImageUsageFlags usageFlags);
		unique_ptr<RenderTarget> CreateDepthRenderTarget(VkExtent2D extent);
		unique_ptr<RenderTarget> CreateColorRenderTarget(VkExtent2D extent, VkFormat format);
	private:
		Device& _device;

		vector<RenderPass*> _renderPasses;

		VkSampleCountFlagBits _msaaSamples = VK_SAMPLE_COUNT_1_BIT;

		vector<unique_ptr<RenderTarget>> _renderTargets;
	};
}

