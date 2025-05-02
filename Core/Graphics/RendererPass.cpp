#include "stdafx.h"
#include "RendererPass.h"
#include "Utils/Utility.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/Framebuffer.h"
#include "Graphics/Vulkans/RenderPass.h"

Core::RendererPass::RendererPass(Device& device)
	:_device{ device }
{
	_renderPass = new RenderPass(device);
	_pipelineState = new PipelineState();

	_threadsReadyCount = 0;

	int coreCount = std::thread::hardware_concurrency();
	_threadCount = std::min(3, coreCount / 2);

	string name = typeid(this).name();
	wstring wName(name.begin(), name.end());
	for (size_t i = 0; i < _threadCount; i++)
	{
		auto workerThread = make_unique<WorkerThread>(_device, &_fenceWait, QueueType::GRAPHICS,
			wName + to_wstring(i));
		_workerThreads.push_back(move(workerThread));
	}
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

void Core::RendererPass::Enqueue(const Job* job)
{
	_workerThreads[_ringIndex]->Enqueue(job);
	_ringIndex = (_ringIndex + 1) % _threadCount;
	_threadsReadyCount = std::min(_threadsReadyCount + 1, _threadCount);
}

void Core::RendererPass::Flush(VkCommandBufferLevel level)
{
	if (_threadsReadyCount <= 0)
		return;

	_timer.tick();

	for (size_t i = 0; i < _threadsReadyCount; i++)
	{
		_workerThreads[i]->Flush(level);
	}

	//todo : split this process from Flush.
	unique_lock<mutex> lock(_lock);
	_fenceWait.wait(lock, [&]
	{
		for (size_t i = 0; i < _threadsReadyCount; i++)
		{
			if (_workerThreads[i]->Complete() == false)
			{
				return false;
			}
		}
		return true;
	});
}
