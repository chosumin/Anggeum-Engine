#pragma once
#include "Vulkans/RenderPass.h"
#include "Vulkans/Framebuffer.h"
#include "Vulkans/CommandBuffer.h"
#include "Vulkans/DescriptorSetBuilder.h"
#include "RenderFrame.h"
#include "Foundation/WorkerThread.h"
#include "Utils/timer.h"

namespace Core
{
	class SwapChain;
	class PipelineState;
	class CommandBuffer;
	class Job;
	class RendererPass
	{
	public:
		RendererPass(Device& device, WorkerThreadManager& workerThreadManager);
		virtual ~RendererPass();

		virtual void Prepare() = 0;
		virtual void Draw(RenderFrame& renderFrame, uint32_t imageIndex) = 0;
	protected:
		void Enqueue(Job* job);
		void Wait();
	protected:
		Device& _device;
		RenderPass* _renderPass;
		PipelineState* _pipelineState;

		vector<Job*> _pendingJobs;
		WorkerThreadManager& _workerThreadManager;
		Core::Timer _timer;
	private:
		condition_variable _fenceWait;
		mutex _lock;
	};
}