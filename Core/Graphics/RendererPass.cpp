#include "stdafx.h"
#include "RendererPass.h"
#include "Utils/Utility.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/RenderPass.h"
#include "Graphics/Vulkans/Buffer.h"

Core::RendererPass::RendererPass(Device& device, WorkerThreadManager& workerThreadManager)
	:_device{ device }, _workerThreadManager(workerThreadManager)
{
	_renderPass = new RenderPass(device);
	_pipelineState = new PipelineState();
}

Core::RendererPass::~RendererPass()
{
	delete(_renderPass);
	delete(_pipelineState);
}

void Core::RendererPass::Enqueue(Job* job)
{
	_pendingJobs.push_back(job);
	job->completionWait = &_fenceWait;
	_workerThreadManager.Enqueue(job);
}

void Core::RendererPass::Wait()
{
	if (_pendingJobs.empty())
		return;

	_timer.tick();

	unique_lock<mutex> lock(_lock);
	_fenceWait.wait(lock, [&]
	{
		for (size_t i = 0; i < _pendingJobs.size(); i++)
		{
			if (_pendingJobs[i]->status != JobStatus::COMPLETE)
			{
				return false;
			}
		}
		return true;
	});
}
