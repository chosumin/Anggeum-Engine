#pragma once
#include "Graphics/RendererPass.h"
#include "Graphics/RendererBatch.h"

namespace Core
{
	class Scene;
	class SwapChain;
	class Material;
	class DepthPrePass : public RendererPass
	{
	public:
		DepthPrePass(Device& device, WorkerThreadManager& workerThreadManager,
			Scene& scene, SwapChain& swapChain, Texture* depthRenderTarget, TransformBatch& transformBatch);
		virtual ~DepthPrePass() override;

		void Prepare() override;
		void Draw(RenderFrame& renderFrame, uint32_t frameIndex, uint32_t imageIndex) override;
	private:
		Scene& _scene;

		unique_ptr<RendererBatches> _rendererBatches;

		shared_ptr<Material> _material;
	};
}

