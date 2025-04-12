#pragma once
#include "VulkanWrapper/RenderPass.h"

namespace Core
{
	class Material;
	class Texture;
	class Pipeline;
	class BrdfLutRenderPass : public RenderPass
	{
	public:
		BrdfLutRenderPass(Device& device, Texture* colorBuffer);
		virtual ~BrdfLutRenderPass() override;

		virtual void Prepare() override;
		virtual void Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex) override;
	private:
		Pipeline* _pipeline;
		Material* _material;
		Texture* _colorRenderTarget;
	};
}