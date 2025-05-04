#include "stdafx.h"
#include "RendererPass.h"
#include "Utils/Utility.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/Framebuffer.h"
#include "Graphics/Vulkans/RenderPass.h"

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
	delete(_framebuffer);
}

void Core::RendererPass::CreateFrameBuffer(SwapChain& swapChain)
{
	_framebuffer = new Framebuffer(_device, swapChain, *_renderPass);
}

void Core::RendererPass::CreateFrameBuffer(Image* image)
{
	_framebuffer = new Framebuffer(_device, *_renderPass, *image);
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
