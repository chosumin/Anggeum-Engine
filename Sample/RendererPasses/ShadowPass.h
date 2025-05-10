#pragma once
#include "Graphics/RendererPass.h"
#include "Graphics/BufferObjects.h"

namespace Core
{
	class Scene;
	class SwapChain;
	class RendererBatch;
	class Material;
	class ShadowPass : public RendererPass
	{
	public:
		ShadowPass(Device& device, WorkerThreadManager& workerThreadManager,
			Scene& scene, SwapChain& swapChain, Texture* depthRenderTarget);
		virtual ~ShadowPass() override;

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
		shared_ptr<Material> _material;

		Texture* _shadowMap;
	};
}

