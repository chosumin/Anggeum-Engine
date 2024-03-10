#include "stdafx.h"
#include "RenderContext.h"
#include "SwapChain.h"
#include "CommandBuffer.h"

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

		CreateCommandPool();
		CreateCommandBuffers();
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

		for (auto commandBuffer : _commandBuffers)
		{
			delete(commandBuffer);
		}

		vkDestroyCommandPool(device, _commandPool, nullptr);
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
		return *_commandBuffers[_currentFrame];
	}

	void RenderContext::Submit(CommandBuffer& commandBuffer)
	{
		Submit({ &commandBuffer });
	}

	void RenderContext::Submit(const vector<CommandBuffer*>& commandBuffers)
	{
		for (auto commandBuffer : commandBuffers)
		{
			commandBuffer->EndCommandBuffer();
			EndFrame(*_swapChain);
		}
	}

	void RenderContext::BeginFrame()
	{
		AcquireSwapChainAndResetCommandBuffer(*_swapChain);

		_commandBuffers[_currentFrame]->BeginCommandBuffer();

		VkViewport viewport;
		VkRect2D scissor;
		_swapChain->GetViewportAndScissor(viewport, scissor);
		_commandBuffers[_currentFrame]->SetViewportAndScissor(viewport, scissor);
	}

	SwapChain& RenderContext::GetSwapChain() const
	{
		return *_swapChain;
	}

	VkExtent2D RenderContext::GetSurfaceExtent() const
	{
		return _swapChain->GetSwapChainExtent();
	}

	void RenderContext::CreateCommandPool()
	{
		QueueFamilyIndices queueFamilyIndices = Device::Instance().FindQueueFamilies();

		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = queueFamilyIndices.GraphicsAndComputeFamily.value();

		auto device = Device::Instance().GetDevice();
		if (vkCreateCommandPool(device, &poolInfo, nullptr, &_commandPool) != VK_SUCCESS)
			throw runtime_error("failed to create command pool!");
	}

	void RenderContext::CreateCommandBuffers()
	{
		vector<VkCommandBuffer> commandBuffers(MAX_FRAMES_IN_FLIGHT);

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = _commandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

		if (vkAllocateCommandBuffers(Device::Instance().GetDevice(), &allocInfo, commandBuffers.data()) != VK_SUCCESS)
			throw runtime_error("failed to allocate command buffers!");

		_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
		for (size_t i = 0; i < _commandBuffers.size(); ++i)
		{
			_commandBuffers[i] = 
				new CommandBuffer(commandBuffers[i]);
		}
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

	void RenderContext::AcquireSwapChainAndResetCommandBuffer(SwapChain& swapChain)
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

		_commandBuffers[_currentFrame]->ResetCommandBuffer();
	}

	void RenderContext::EndFrame(SwapChain& swapChain)
	{
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore waitSemaphores[] = { _imageAvailableSemaphores[_currentFrame] };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;

		//todo : handle multi thread 
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &_commandBuffers[_currentFrame]->GetHandle();

		VkSemaphore signalSemaphores[] = { _renderFinishedSemaphores[_currentFrame] };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		if (vkQueueSubmit(_device.GetGraphicsQueue(), 1, &submitInfo, _inFlightFences[_currentFrame]) != VK_SUCCESS)
			throw runtime_error("failed to submit draw command buffer!");

		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;

		VkSwapchainKHR swapChains[] = { swapChain.GetSwapChain() };
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
