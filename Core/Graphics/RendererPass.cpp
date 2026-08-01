#include "stdafx.h"
#include "RendererPass.h"

Core::RendererPass::RendererPass(Device& device, WorkerThreadManager& workerThreadManager)
	: Threadable(workerThreadManager), _device{ device }
{
	_renderPass = new RenderPass(device);
	_pipelineState = new PipelineState();
}

Core::RendererPass::~RendererPass()
{
	delete(_renderPass);
	delete(_pipelineState);
}

void Core::RendererPass::Wait()
{
	_timer.tick();

	WaitForJobs();
}
