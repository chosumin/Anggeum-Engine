#pragma once
#include "RenderPass.h"

namespace Core
{
	class Scene;
	class SwapChain;
	class RendererBatch;
	class PerspectiveCamera;
	class InstanceBuffer;
	class Material;
	class ShadowRenderPass : public RenderPass
	{
	public:
		ShadowRenderPass(Device& device,
			Scene& scene, SwapChain& swapChain, RenderTarget* depthRenderTarget);
		virtual ~ShadowRenderPass() override;

		virtual void Prepare() override;
		virtual void Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex) override;
	private:
		Scene& _scene;

		PerspectiveCamera* _shadowCamera;
		unordered_map<type_index, RendererBatch*> _batches;
		InstanceBuffer* _instanceBuffer;
		Material* _material;
	};
}

