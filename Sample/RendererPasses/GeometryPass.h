#pragma once
#include "Graphics/RendererPass.h"
#include "Graphics/BufferObjects.h"

namespace Core
{
	class Scene;
	class SwapChain;
	class RendererBatch;
	class Pipeline;
	class GeometryPass : public RendererPass
	{
	public:
		GeometryPass(Device& device, WorkerThreadManager& workerThreadManager,
			Scene& scene, SwapChain& swapChain, 
			shared_ptr<Texture> colorRenderTarget, shared_ptr<Texture> depthRenderTarget, 
			shared_ptr<Texture> shadowRenderTarget, 
			shared_ptr<Texture> pregenerationSky, shared_ptr<Texture> environmentCubemap,
			shared_ptr<Texture> prefilterCubemap, shared_ptr<Texture> brdfLut);
		virtual ~GeometryPass() override;

		virtual void Prepare() override;
		virtual void Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex) override;

		//TODO : remove and replace it to the light component
		void SetBuffer(ShadowUniform& shadowBuffer)
		{
			_shadowBuffer = &shadowBuffer;
		}
	private:
		void PreparePregenerationSkybox(Texture* pregenerationSky, 
			Texture* irradianceCubemap, Texture* prefilterCubemap);
		void DrawSkybox(CommandBuffer& commandBuffer, uint32_t currentFrame);
		void UpdateGUI();
		void UpdateLightBuffer();
	private:
		Scene& _scene;

		unordered_map<uint32_t, RendererBatch*> _batches;

		shared_ptr<Texture> _shadowRenderTarget;
		ShadowUniform* _shadowBuffer;
		LightBuffer _lightBuffer;
		Pipeline* _skyboxPipeline;

		shared_ptr<Texture> _irradianceCubemap;
		shared_ptr<Texture> _prefilteredCubemap;
		shared_ptr<Texture> _brdfLut;
	};
}

