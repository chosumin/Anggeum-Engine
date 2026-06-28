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

		virtual void Draw(RenderFrame& renderFrame, uint32_t imageIndex) override;
		virtual void OnGUI(RenderFrame& renderFrame) override;

		void Cleanup();
		void Resize(SwapChain& swapChain);

		virtual VkSampleCountFlagBits GetMSAASamples() const override 
		{ 
			return _msaaSamples; 
		}
	private:
		VkSampleCountFlagBits GetMaxUsableSampleCount();

		void AddRendererPass(RendererPass* renderPass)
		{
			_rendererPasses.push_back(renderPass);
		}

		void CreateLightCullingBuffer(VkExtent2D extent, ivec2 tileNums);
		void CreateTransformBuffer(Scene& scene);
	private:
		Device& _device;
		vector<RendererPass*> _rendererPasses;
		VkSampleCountFlagBits _msaaSamples = VK_SAMPLE_COUNT_1_BIT;
		Buffer* _lightBuffer;
		ShadowUniform _shadowBuffer;
		TransformBatch _transformBatch;
	};
}

