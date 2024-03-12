#include "stdafx.h"
#include "RenderContext.h"
#include "SwapChain.h"
#include "CommandBuffer.h"
#include "CommandPool.h"

namespace Core
{
	vector<function<void(SwapChain&)>> RenderContext::_resizeCallbacks;

	void RenderContext::AddResizeCallback(
		function<void(SwapChain&)> callback)
	{
		_resizeCallbacks.push_back(callback);
	}

	void Core::RenderContext::RemoveResizeCallback(
		function<void(SwapChain&)> callback)
	{
		for (auto it = _resizeCallbacks.begin();
			it != _resizeCallbacks.end(); ++it) {
			bool isSame = is_same<
				decltype(*it), decltype(callback)>::value;

			if (isSame)
			{
				_resizeCallbacks.erase(it);
				return;
			}
		}
	}

	RenderContext::RenderContext(Device& device)
		:_device(device)
	{
		_swapChain = new SwapChain();

		auto queueFamilyIndices = device.FindQueueFamilies();
		_commandPool = new CommandPool(device,
			queueFamilyIndices.GraphicsAndComputeFamily.value());

		CreateSyncObjects();
	}

	RenderContext::~RenderContext()
	{
		auto device = _device.GetDevice();

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		{
			vkDestroySemaphore(device, _imageAvailableSemaphores[i], nullptr);
			vkDestroySemaphore(device, _renderFinishedSemaphores[i], nullptr);
			vkDestroyFence(device, _inFlightFences[i], nullptr);
		}

		delete(_swapChain);
		delete(_commandPool);
	}

	void RenderContext::Prepare(size_t threadCount)
	{
	}

	void RenderContext::RecreateSwapChain()
	{
		_swapChain->RecreateSwapChain();
		for (auto& resize : _resizeCallbacks)
		{
			resize(*_swapChain);
		}
	}

	CommandBuffer& RenderContext::Begin()
	{
		BeginFrame();

		auto& commandBuffer = _commandPool->RequestCommandBuffer(_currentFrame);
		commandBuffer.BeginCommandBuffer();

		VkViewport viewport;
		VkRect2D scissor;
		_swapChain->GetViewportAndScissor(viewport, scissor);

		commandBuffer.SetViewportAndScissor(viewport, scissor);

		return commandBuffer;
	}

	void RenderContext::Submit(CommandBuffer& commandBuffer)
	{
		Submit({ &commandBuffer });
	}

	void RenderContext::Submit(const vector<CommandBuffer*>& commandBuffers)
	{
		std::vector<VkCommandBuffer> cmdHandles(commandBuffers.size(), VK_NULL_HANDLE);
		std::transform(commandBuffers.begin(), commandBuffers.end(),
			cmdHandles.begin(), [](const CommandBuffer* cmd) { return cmd->GetHandle(); });

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore waitSemaphores[] = { _imageAvailableSemaphores[_currentFrame] };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;

		submitInfo.commandBufferCount = static_cast<uint32_t>(cmdHandles.size());
		submitInfo.pCommandBuffers = cmdHandles.data();

		VkSemaphore signalSemaphores[] = { _renderFinishedSemaphores[_currentFrame] };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		if (vkQueueSubmit(_device.GetGraphicsQueue(), 1, &submitInfo, _inFlightFences[_currentFrame]) != VK_SUCCESS)
			throw runtime_error("failed to submit draw command buffer!");

		EndFrame(signalSemaphores);
	}

	void RenderContext::BeginFrame()
	{
		AcquireSwapChainAndResetFence(*_swapChain);

		_commandPool->ResetCommandBuffers(_currentFrame);
	}

	SwapChain& RenderContext::GetSwapChain() const
	{
		return *_swapChain;
	}

	VkExtent2D RenderContext::GetSurfaceExtent() const
	{
		return _swapChain->GetSwapChainExtent();
	}

	void RenderContext::CreateSyncObjects()
	{
		_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
		_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
		_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		auto device = Device::Instance().GetDevice();

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		{
			if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &_imageAvailableSemaphores[i]) != VK_SUCCESS ||
				vkCreateSemaphore(device, &semaphoreInfo, nullptr, &_renderFinishedSemaphores[i]) != VK_SUCCESS ||
				vkCreateFence(device, &fenceInfo, nullptr, &_inFlightFences[i]) != VK_SUCCESS)
				throw runtime_error("failed to create semaphores!");
		}
	}

	void RenderContext::AcquireSwapChainAndResetFence(SwapChain& swapChain)
	{
		auto device = Device::Instance().GetDevice();

		vkWaitForFences(device, 1, &_inFlightFences[_currentFrame], VK_TRUE, UINT64_MAX);

		auto swapChainHandle = swapChain.GetSwapChain();
		VkResult result = vkAcquireNextImageKHR(
			device, swapChainHandle, UINT64_MAX,
			_imageAvailableSemaphores[_currentFrame], VK_NULL_HANDLE, &_imageIndex);

		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			RecreateSwapChain();
			return;
		}
		else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
			throw runtime_error("failed to acquire swap chain image!");

		vkResetFences(device, 1, &_inFlightFences[_currentFrame]);
	}

	void RenderContext::EndFrame(VkSemaphore* semaphore)
	{
		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = semaphore;

		VkSwapchainKHR swapChains[] = { _swapChain->GetSwapChain() };
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapChains;
		presentInfo.pImageIndices = &_imageIndex;

		VkResult result = vkQueuePresentKHR(Device::Instance().GetPresentQueue(), &presentInfo);

		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || Window::FramebufferResized)
		{
			Window::FramebufferResized = false;
			RecreateSwapChain();
		}
		else if (result != VK_SUCCESS)
			throw runtime_error("failed to present swap chain image!");

		_currentFrame = (_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
	}
}
