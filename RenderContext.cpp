#include "stdafx.h"
#include "RenderContext.h"
#include "SwapChain.h"
#include "CommandBuffer.h"

namespace Core
{
	RenderContext::RenderContext(Device& device)
		:_device(device)
	{
		_swapChain = new SwapChain();
		_commandBuffer = new CommandBuffer();
	}

	RenderContext::~RenderContext()
	{
		delete(_swapChain);
		delete(_commandBuffer);
	}

	void RenderContext::Prepare(size_t threadCount)
	{
	}

	void RenderContext::Recreate()
	{
	}

	void RenderContext::RecreateSwapChain()
	{
	}

	CommandBuffer& RenderContext::Begin()
	{
		BeginFrame();
		return *_commandBuffer;
	}

	void RenderContext::Submit(CommandBuffer& commandBuffer)
	{
		Submit({ &commandBuffer });
	}

	void RenderContext::Submit(const vector<CommandBuffer*>& commandBuffers)
	{
		for (auto commandBuffer : commandBuffers)
		{
			commandBuffer->EndFrame(*_swapChain);
		}
	}

	void RenderContext::BeginFrame()
	{
		_commandBuffer->AcquireSwapChainAndResetCommandBuffer(*_swapChain);
		_commandBuffer->BeginCommandBuffer();

		VkViewport viewport;
		VkRect2D scissor;
		_swapChain->GetViewportAndScissor(viewport, scissor);
		_commandBuffer->SetViewportAndScissor(viewport, scissor);
	}

	SwapChain& RenderContext::GetSwapChain() const
	{
		return *_swapChain;
	}

	VkExtent2D RenderContext::GetSurfaceExtent() const
	{
		return _swapChain->GetSwapChainExtent();
	}

	uint32_t RenderContext::GetActiveFrameIndex() const
	{
		return _commandBuffer->GetCurrentFrame();
	}
}
