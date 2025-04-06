#pragma once
#include "VulkanWrapper/RenderPass.h"
#include "BufferObjects/BufferObjects.h"

namespace Core
{
	class Scene;
	class SwapChain;
	class RendererBatch;
	class Material;
	class ShadowRenderPass : public RenderPass
	{
	public:
		ShadowRenderPass(Device& device,
			Scene& scene, SwapChain& swapChain, Texture* depthRenderTarget);
		virtual ~ShadowRenderPass() override;

		virtual void Prepare() override;
		virtual void Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex) override;

		ShadowUniform& GetShadowBuffer()
		{
			return _shadowBuffer;
		}
	private:
		void UpdateGUI();
	private:
		Scene& _scene;

		VPBufferObject _directionalLight;
		ShadowUniform _shadowBuffer;

		RendererBatch* _batch;
		Material* _material;

		Texture* _shadowMap;
	};
}

