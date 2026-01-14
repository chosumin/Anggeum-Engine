#pragma once
#include "Graphics/RendererPass.h"
#include "Graphics/BufferObjects.h"
#include "Graphics/RendererBatch.h"

namespace Core
{
	class Scene;
	class SwapChain;
	class Material;
	class ShadowPass : public RendererPass
	{
	public:
		ShadowPass(Device& device, WorkerThreadManager& workerThreadManager,
			Scene& scene, SwapChain& swapChain, Texture* depthRenderTarget, TransformBatch& transformBatch);
		virtual ~ShadowPass() override;

		virtual void Prepare() override;
		virtual void Draw(CommandBuffer& commandBuffer, CommandBuffer& computeBuffer, uint32_t currentFrame, uint32_t imageIndex) override;

		ShadowUniform& GetShadowBuffer()
		{
			return _shadowBuffer;
		}
	private:
		void UpdateGUI();
	private:
		Scene& _scene;

		CameraBuffer _directionalLight;
		ShadowUniform _shadowBuffer;

		unique_ptr<RendererBatches> _rendererBatches;
		shared_ptr<Material> _material;

		Texture* _shadowMap;
	};
}

