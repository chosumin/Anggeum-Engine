#pragma once
#include "Graphics/IRenderPipeline.h"
#include "Graphics/BufferObjects.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/Vulkans/BindlessTextureManager.h"

namespace Core
{
	class RendererPass;
	class Scene;
	class SwapChain;
	class WorkerThreadManager;
	class TransferContext;
	class Buffer;
	class RenderContext;

	class ForwardRenderPipeline : public IRenderPipeline
	{
	public:
		ForwardRenderPipeline(Device& device, 
			WorkerThreadManager& workerThreadManager, Scene& scene, SwapChain& swapChain);
		virtual ~ForwardRenderPipeline() override;

		virtual void Prepare() override;
		virtual void Draw(RenderFrame& renderFrame, uint32_t imageIndex) override;

		void Cleanup();
		void Resize(SwapChain& swapChain);

		virtual VkSampleCountFlagBits GetMSAASamples() const override 
		{ 
			return _msaaSamples; 
		}
		virtual Texture* GetColorRenderTarget() override 
		{ 
			return _renderTargets[0].get(); 
		}

		void RegisterGiTexturesToBindless(RenderContext& renderContext);
	private:
		VkSampleCountFlagBits GetMaxUsableSampleCount();

		void AddRendererPass(RendererPass* renderPass)
		{
			_rendererPasses.push_back(renderPass);
		}

		shared_ptr<Texture> CreateRenderTarget(VkExtent2D extent, VkFormat format,
			VkImageLayout layout, VkImageUsageFlags usageFlags);
		shared_ptr<Texture> CreateDepthRenderTarget(VkExtent2D extent, bool isUsedAsSource, VkSampleCountFlagBits sampleCount, bool isStorageImage = false);
		shared_ptr<Texture> CreateColorRenderTarget(VkExtent2D extent, VkFormat format, bool isUsedAsSource, bool isStorageImage = false);

		void CreatePreSkyTextures();
		void CreateLightCullingBuffer(VkExtent2D extent, ivec2 tileNums);
		void CreateTransformBuffer(Scene& scene);
	private:
		Device& _device;
		vector<RendererPass*> _rendererPasses;
		VkSampleCountFlagBits _msaaSamples = VK_SAMPLE_COUNT_1_BIT;
		Buffer* _lightBuffer;

		TransformBatch _transformBatch;

		GI _giBuffer;

		// Render target 이름 상수
		static constexpr const char* RT_MAIN_COLOR = "MainColor";
		static constexpr const char* RT_MAIN_DEPTH = "MainDepth";
		static constexpr const char* RT_SHADOW_DEPTH = "ShadowDepth";
		static constexpr const char* RT_OFFSCREEN = "Offscreen";
		static constexpr const char* RT_IRRADIANCE = "Irradiance";
		static constexpr const char* RT_PREFILTERED = "Prefiltered";
		static constexpr const char* RT_BRDF_LUT = "BrdfLut";

		// _renderTargets 제거, RenderFrame에서 관리
		void InitializeRenderTargets(RenderContext& renderContext, VkExtent2D extent, VkFormat swapChainFormat);
	};
}

