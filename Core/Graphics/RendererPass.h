#pragma once
#include "Vulkans/RenderPass.h"
#include "Vulkans/Framebuffer.h"
#include "Vulkans/CommandBuffer.h"
#include "RenderFrame.h"
#include "Foundation/WorkerThread.h"

namespace Core
{
	class RendererPass
	{
	public:
		RendererPass(Device& device, WorkerThreadManager& workerThreadManager);
		virtual ~RendererPass();

		virtual void EnsureRenderTargets(RenderFrame& renderFrame) {}
		virtual void Draw(RenderFrame& renderFrame, uint32_t imageIndex) = 0;
		virtual void OnGUI(RenderFrame& renderFrame) {}
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