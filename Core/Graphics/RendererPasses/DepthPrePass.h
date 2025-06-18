#pragma once
#include "Graphics/RendererPass.h"

namespace Core
{
	class Scene;
	class SwapChain;
	class RendererBatch;
	class Material;
	class DepthPrePass : public RendererPass
	{
	public:
		DepthPrePass(Device& device, WorkerThreadManager& workerThreadManager,
			Scene& scene, SwapChain& swapChain, Texture* depthRenderTarget);
		virtual ~DepthPrePass() override;

		void Prepare() override;
		void Draw(CommandBuffer& commandBuffer, CommandBuffer& computeBuffer, uint32_t currentFrame, uint32_t imageIndex) override;
	private:
		Scene& _scene;

		RendererBatch* _batch;
		shared_ptr<Material> _material;
	};
}

