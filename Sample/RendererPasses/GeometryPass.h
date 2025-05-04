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
			Texture* colorRenderTarget, Texture* depthRenderTarget, 
			Texture* shadowRenderTarget, 
			Texture* pregenerationSky, Texture* environmentCubemap,
			Texture* prefilterCubemap, Texture* brdfLut);
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

		unordered_map<type_index, RendererBatch*> _batches;

		Texture* _shadowRenderTarget;
		ShadowUniform* _shadowBuffer;
		LightBuffer _lightBuffer;
		Pipeline* _skyboxPipeline;

		Texture* _irradianceCubemap;
		Texture* _prefilteredCubemap;
		Texture* _brdfLut;
	};
}

