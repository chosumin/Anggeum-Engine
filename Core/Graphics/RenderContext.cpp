#include "stdafx.h"
#include "Graphics/RenderContext.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/CommandPool.h"

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
		_swapChain = new SwapChain(device);

		auto queueFamilyIndices = device.GetQueueFamilyIndices();
		
		_commandPool = new CommandPool(
			device, queueFamilyIndices.GraphicsFamily.value());

		_computeCommandPool = new CommandPool(
			device, queueFamilyIndices.ComputeFamily.value());

		CreateSyncObjects();
	}

	RenderContext::~RenderContext()
	{
		auto device = _device.GetDevice();

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		{
			vkDestroySemaphore(device, _imageAvailableSemaphores[i], nullptr);
			vkDestroySemaphore(device, _renderFinishedSemaphores[i], nullptr);
		}

		vkDestroySemaphore(device, _graphicsSemaphore, nullptr);
		vkDestroySemaphore(device, _computeSemaphore, nullptr);

		delete(_swapChain);
		delete(_commandPool);
		delete(_computeCommandPool);
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

	vector<CommandBuffer> RenderContext::Begin()
	{
		AcquireSwapChainAndResetFence(*_swapChain);

		auto& commandBuffer = _commandPool->RequestCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
		commandBuffer.BeginCommandBuffer();

		auto& computeBuffer = _commandPool->RequestCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
		computeBuffer.BeginCommandBuffer();

		vector<CommandBuffer> buffers;
		buffers.push_back(commandBuffer);
		buffers.push_back(computeBuffer);

		return buffers;
	}

	void RenderContext::Submit(CommandBuffer& commandBuffer, CommandBuffer& computeBuffer)
	{
		u32 waitSemaphoreCount = 1;

		bool waitForComputeSemaphore = _lastComputeSemaphoreValue > 0;
		if (waitForComputeSemaphore)
			waitSemaphoreCount++;

		bool waitForTimelineSemaphore = FrameCounter::GetFrameNumber() >= _maxFramesInFlight;
		if (waitForTimelineSemaphore)
			waitSemaphoreCount++;

		VkSemaphore waitSemaphores[] = 
		{
			_imageAvailableSemaphores[_currentFrame],
			_computeSemaphore,
			_graphicsSemaphore 
		};

		VkPipelineStageFlags waitStages[] =
		{
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
		};

		VkSemaphore signalSemaphores[] =
		{
			_renderFinishedSemaphores[_currentFrame],
			_graphicsSemaphore
		};

		u64 signalValues[] = { 0, FrameCounter::GetFrameNumber() + 1 };

		VkTimelineSemaphoreSubmitInfo semaphoreSubmitInfo{};
		semaphoreSubmitInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
		semaphoreSubmitInfo.signalSemaphoreValueCount = 2;
		semaphoreSubmitInfo.pSignalSemaphoreValues = signalValues;

		u64 waitValues[] = 
		{ 
			0, _lastComputeSemaphoreValue, 
			FrameCounter::GetFrameNumber() - (_maxFramesInFlight - 1)
		};

		semaphoreSubmitInfo.waitSemaphoreValueCount = waitSemaphoreCount;
		semaphoreSubmitInfo.pWaitSemaphoreValues = waitValues;

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		submitInfo.waitSemaphoreCount = waitSemaphoreCount;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer.GetHandle();
		submitInfo.signalSemaphoreCount = 2;
		submitInfo.pSignalSemaphores = signalSemaphores;

		submitInfo.pNext = &semaphoreSubmitInfo;

		if (vkQueueSubmit(_device.GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
			throw runtime_error("failed to submit draw command buffer!");

		SubmitComputeBuffer(computeBuffer);

		EndFrame(signalSemaphores);
	}

	void RenderContext::SubmitComputeBuffer(CommandBuffer& computeBuffer)
	{
		bool has_wait_semaphore = _lastComputeSemaphoreValue > 0;

		VkSemaphore waitSemaphores[] = { _computeSemaphore };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT };

		VkSemaphore signalSemaphores[] = { _computeSemaphore };

		VkTimelineSemaphoreSubmitInfo semaphore_info{};
		semaphore_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;

		u64 waitValues[] = { _lastComputeSemaphoreValue };
		semaphore_info.waitSemaphoreValueCount = has_wait_semaphore ? 1 : 0;
		semaphore_info.pWaitSemaphoreValues = waitValues;

		++_lastComputeSemaphoreValue;

		u64 signalValues[] = { _lastComputeSemaphoreValue };
		semaphore_info.signalSemaphoreValueCount = 1;
		semaphore_info.pSignalSemaphoreValues = signalValues;

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.waitSemaphoreCount = has_wait_semaphore ? 1 : 0;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &computeBuffer.GetHandle();
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;
		submitInfo.pNext = &semaphore_info;

		if (vkQueueSubmit(_device.GetComputeQueue(), 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
			throw runtime_error("failed to submit compute command buffer!");
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

		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		auto device = _device.GetDevice();

		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		{
			if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &_imageAvailableSemaphores[i]) != VK_SUCCESS)
				throw runtime_error("failed to create semaphores!");
			
			if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &_renderFinishedSemaphores[i]) != VK_SUCCESS)
				throw runtime_error("failed to create semaphores!");
		}

		VkSemaphoreTypeCreateInfo semaphoreTypeInfo{};
		semaphoreTypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
		semaphoreTypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		semaphoreInfo.pNext = &semaphoreTypeInfo;

		vkCreateSemaphore(device, &semaphoreInfo, nullptr, &_graphicsSemaphore);
		vkCreateSemaphore(device, &semaphoreInfo, nullptr, &_computeSemaphore);
	}

	void RenderContext::AcquireSwapChainAndResetFence(SwapChain& swapChain)
	{
		auto device = _device.GetDevice();

		if (FrameCounter::GetFrameNumber() >= _maxFramesInFlight)
		{
			u64 graphicsTimelineValue = FrameCounter::GetFrameNumber() - (_maxFramesInFlight - 1);
			u64 computeTimelineValue = _lastComputeSemaphoreValue;

			u64 waitValues[] = { graphicsTimelineValue, computeTimelineValue };
			VkSemaphore waitSemaphores[] = { _graphicsSemaphore, _computeSemaphore };

			VkSemaphoreWaitInfo waitInfo{};
			waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
			waitInfo.semaphoreCount = 2;
			waitInfo.pSemaphores = waitSemaphores;
			waitInfo.pValues = waitValues;

			vkWaitSemaphores(device, &waitInfo, UINT64_MAX);
		}

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

		VkResult result = vkQueuePresentKHR(_device.GetPresentQueue(), &presentInfo);

		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || Window::FramebufferResized)
		{
			Window::FramebufferResized = false;
			RecreateSwapChain();
		}
		else if (result != VK_SUCCESS)
			throw runtime_error("failed to present swap chain image!");

		FrameCounter::IncreaseFrame();
		_currentFrame = (_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
	}
}
