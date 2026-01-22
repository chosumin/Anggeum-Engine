#pragma once
#include "Graphics/RendererPass.h"
#include "Graphics/BufferObjects.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/RendererBatch.h"

namespace Core
{
	class Scene;
	class SwapChain;
	class Pipeline;
	class GeometryPass : public RendererPass
	{
	public:
		GeometryPass(Device& device, WorkerThreadManager& workerThreadManager,
			Scene& scene, SwapChain& swapChain, 
			shared_ptr<Texture> colorRenderTarget, shared_ptr<Texture> depthRenderTarget, 
			shared_ptr<Texture> shadowRenderTarget, 
			shared_ptr<Texture> pregenerationSky, shared_ptr<Texture> environmentCubemap,
			shared_ptr<Texture> prefilterCubemap, shared_ptr<Texture> brdfLut,
			Buffer* lightVisibilityBuffer, ivec2 tileNums,
			TransformBatch& transformBatch);
		virtual ~GeometryPass() override;

		virtual void Prepare() override;
		virtual void Draw(RenderFrame& renderFrame, uint32_t imageIndex) override;

		//TODO : remove and replace it to the light component
		void SetBuffer(ShadowUniform& shadowBuffer)
		{
			_shadowBuffer = &shadowBuffer;
		}
	private:
		void PreparePregenerationSkybox(Texture* pregenerationSky, 
			Texture* irradianceCubemap, Texture* prefilterCubemap);
		void DrawSkybox(RenderFrame& renderFrame, CommandBuffer& commandBuffer);
		void UpdateGUI();
		void UpdateLightBuffer();
	private:
		Scene& _scene;

		unique_ptr<RendererBatches> _rendererBatches;

		shared_ptr<Texture> _shadowRenderTarget;
		ShadowUniform* _shadowBuffer;
		LightBuffer _lightBuffer;
		Pipeline* _skyboxPipeline;

		shared_ptr<Texture> _irradianceCubemap;
		shared_ptr<Texture> _prefilteredCubemap;
		shared_ptr<Texture> _brdfLut;

		Core::Buffer* _lightVisibilityBuffer;
		TileInfo _tileInfo;
	};
}

