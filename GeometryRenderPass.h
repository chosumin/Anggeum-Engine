#pragma once
#include "RenderPass.h"

namespace Core
{
	class Scene;
	class Camera;
	class Material;
	class Pipeline;
	class Framebuffer;
	class SwapChain;
	class GeometryRenderPass : public RenderPass
	{
	public:
		GeometryRenderPass(Device& device, 
			VkExtent2D extent,
			Scene& scene, SwapChain& swapChain);
		virtual ~GeometryRenderPass() override;

		virtual void Prepare(CommandPool& commandPool) override;
		virtual void Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex) override;
	private:
		Scene& _scene;
		Material* _material;
		Pipeline* _pipeline;
	};
}

