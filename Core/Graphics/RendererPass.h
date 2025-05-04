#pragma once
#include "Graphics/Vulkans/PipelineState.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/RenderPass.h"
#include "Graphics/Vulkans/Framebuffer.h"
#include "Foundation/WorkerThread.h"
#include "Utils/timer.h"

namespace Core
{
	class SwapChain;
	class RenderPass;
	class Framebuffer;
	class PipelineState;
	class CommandBuffer;
	class Job;
	class RendererPass
	{
	public:
		RendererPass(Device& device, WorkerThreadManager& workerThreadManager);
		virtual ~RendererPass();

		virtual void Prepare() = 0;
		virtual void Draw(CommandBuffer& commandBuffer,
			uint32_t currentFrame, uint32_t imageIndex) = 0;
	protected:
		void CreateFrameBuffer(SwapChain& swapChain);
		void CreateFrameBuffer(Image* image);

		void Enqueue(Job* job);
		void Wait();
	protected:
		Device& _device;
		RenderPass* _renderPass;
		Framebuffer* _framebuffer;
		PipelineState* _pipelineState;

		vector<Job*> _pendingJobs;
		WorkerThreadManager& _workerThreadManager;
		Core::Timer _timer;
	private:
		condition_variable _fenceWait;
		mutex _lock;
	};
}