#pragma once
#include "Vulkans/RenderPass.h"
#include "Vulkans/Framebuffer.h"
#include "Vulkans/CommandBuffer.h"
#include "Vulkans/SubmitInfo.h"
#include "RenderFrame.h"
#include "SyncContext.h"
#include "Foundation/WorkerThread.h"

namespace Core
{
	class RendererPass
	{
	public:
		RendererPass(Device& device, WorkerThreadManager& workerThreadManager);
		virtual ~RendererPass();

		virtual void EnsureRenderTargets(RenderFrame& renderFrame) {}
		virtual void Draw(RenderFrame& renderFrame, SyncContext& syncContext, CommandBuffer& commandBuffer, uint32_t imageIndex) = 0;
		virtual void OnGUI(RenderFrame& renderFrame) {}

		virtual QueueType GetQueueType() const { return QueueType::Graphics; }
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