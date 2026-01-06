#pragma once
#include "Graphics/Vulkans/PipelineState.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/RenderPass.h"
#include "Graphics/Vulkans/Framebuffer.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Foundation/WorkerThread.h"
#include "Utils/timer.h"

namespace Core
{
	class SwapChain;
	class RenderPass;
	class PipelineState;
	class CommandBuffer;
	class Job;
	class RendererPass
	{
	public:
		RendererPass(Device& device, WorkerThreadManager& workerThreadManager);
		virtual ~RendererPass();

		virtual void Prepare() = 0;
		virtual void Draw(CommandBuffer& commandBuffer, CommandBuffer& computeBuffer,
			uint32_t currentFrame, uint32_t imageIndex) = 0;
	protected:
		void CreateFrameBuffer(SwapChain& swapChain);
		void CreateFrameBuffer(Image* image);
		void CreateInstanceBuffer();

		void Enqueue(Job* job);
		void Wait();
	protected:
		Device& _device;
		RenderPass* _renderPass;
		Framebuffer* _framebuffer;
		PipelineState* _pipelineState;

		Core::Buffer* _instanceBuffer;

		vector<Job*> _pendingJobs;
		WorkerThreadManager& _workerThreadManager;
		Core::Timer _timer;
	private:
		condition_variable _fenceWait;
		mutex _lock;
	};
}