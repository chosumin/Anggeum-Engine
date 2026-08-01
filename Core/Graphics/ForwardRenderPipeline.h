#pragma once
#include "Graphics/IRenderPipeline.h"
#include "Graphics/BufferObjects.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/Vulkans/BindlessTextureManager.h"

namespace Core
{
	class Scene;
	class SwapChain;
	class WorkerThreadManager;
	class TransferContext;
	class Buffer;
	class RenderContext;
	class FrameGraph;

	class ForwardRenderPipeline : public IRenderPipeline
	{
	public:
		ForwardRenderPipeline(Device& device, 
			WorkerThreadManager& workerThreadManager, Scene& scene, SwapChain& swapChain);
		virtual ~ForwardRenderPipeline() override;

		virtual void Draw(RenderContext& renderContext, RenderFrame& renderFrame, uint32_t imageIndex) override;
		virtual void OnGUI(RenderFrame& renderFrame) override;

		void Cleanup();
		void Resize(SwapChain& swapChain);

		virtual VkSampleCountFlagBits GetMSAASamples() const override 
		{ 
			return _msaaSamples; 
		}
	private:
		VkSampleCountFlagBits GetMaxUsableSampleCount();

		// Fills the uniform blocks every pass in this frame shares.
		void UploadSharedUniforms(RenderFrame& renderFrame);

	private:
		Device& _device;
		Scene& _scene;
		VkExtent2D _swapChainExtents;

		unique_ptr<FrameGraph> _frameGraph;
		VkSampleCountFlagBits _msaaSamples = VK_SAMPLE_COUNT_1_BIT;
	};
}

