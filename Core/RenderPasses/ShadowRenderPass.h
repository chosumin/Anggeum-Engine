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
			Scene& scene, SwapChain& swapChain, RenderTarget* depthRenderTarget);
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

		unordered_map<type_index, RendererBatch*> _batches;
		Material* _material;

		RenderTarget* _shadowMap;
	};
}

