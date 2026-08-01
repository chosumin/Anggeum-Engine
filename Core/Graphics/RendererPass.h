#pragma once
#include "Vulkans/RenderPass.h"
#include "Vulkans/Framebuffer.h"
#include "Vulkans/CommandBuffer.h"
#include "Vulkans/SubmitInfo.h"
#include "RenderFrame.h"
#include "SyncContext.h"
#include "Foundation/WorkerThread.h"
#include "Foundation/Threadable.h"

namespace Core
{
	class RendererPass : public Threadable
	{
	public:
		RendererPass(Device& device, WorkerThreadManager& workerThreadManager);
		virtual ~RendererPass();

		virtual void EnsureRenderTargets(RenderFrame& renderFrame) {}
		virtual void Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer, uint32_t imageIndex) = 0;
		virtual void OnGUI(RenderFrame& renderFrame) {}

		virtual QueueType GetQueueType() const { return QueueType::Graphics; }
	protected:
		// Threadable::WaitForJobs plus the pass-side generation timer.
		void Wait();
	protected:
		Device& _device;
		RenderPass* _renderPass;
		PipelineState* _pipelineState;

		Core::Timer _timer;
	};
}