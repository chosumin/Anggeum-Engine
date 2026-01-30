#include "stdafx.h"
#include "RenderContext.h"
#include "RenderFrame.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/CommandPool.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/BindlessTextureManager.h"
using namespace Core;

vector<function<void(SwapChain&)>> RenderContext::_resizeCallbacks;

void RenderContext::AddResizeCallback(function<void(SwapChain&)> callback)
{
	_resizeCallbacks.push_back(callback);
}

void RenderContext::RemoveResizeCallback(function<void(SwapChain&)> callback)
{
	for (auto it = _resizeCallbacks.begin(); it != _resizeCallbacks.end(); ++it)
	{
		bool isSame = is_same<decltype(*it), decltype(callback)>::value;
		if (isSame)
		{
			_resizeCallbacks.erase(it);
			return;
		}
	}
}

RenderContext::RenderContext(Device& device)
	: _device(device)
{
	_swapChain = new SwapChain(device);

	// Create bindless texture manager if supported
	if (device.SupportsDescriptorIndexing())
	{
		_bindlessTextureManager = make_unique<BindlessTextureManager>(device, 4096);
	}

	if (_enableGpuDrivenRendering)
	{
		_meshBufferManager = make_unique<MeshBufferManager>(_device);
		_materialManager = make_unique<MaterialManager>();
	}
	auto queueFamilyIndices = device.GetQueueFamilyIndices();

	// Create command pools (owned by RenderContext)
	_commandPool = new CommandPool(device, queueFamilyIndices.GraphicsFamily.value());
	_computeCommandPool = new CommandPool(device, queueFamilyIndices.ComputeFamily.value());

	CreateRenderFrames();
	CreateSyncObjects();	
}

RenderContext::~RenderContext()
{
	_meshBufferManager.reset();
	_materialManager.reset();

	auto device = _device.GetDevice();

	// Clean up timeline semaphores
	if (_graphicsSemaphore != VK_NULL_HANDLE)
		vkDestroySemaphore(device, _graphicsSemaphore, nullptr);

	if (_computeSemaphore != VK_NULL_HANDLE)
		vkDestroySemaphore(device, _computeSemaphore, nullptr);

	// Clean up frames
	_frames.clear();

	// Clean up command pools
	delete _commandPool;
	delete _computeCommandPool;

	// Clean up swap chain
	delete _swapChain;
}

void RenderContext::CreateRenderFrames()
{
	_frames.resize(MAX_FRAMES_IN_FLIGHT);

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		_frames[i] = make_unique<RenderFrame>(_device, _bindlessTextureManager.get());
	}
}

void RenderContext::Prepare()
{
	if (_bindlessTextureManager)
	{
		_bindlessTextureManager->Initialize();
		cout << "Bindless texture system initialized with "
			<< _bindlessTextureManager->GetMaxTextures() << " slots" << endl;
	}
}

void RenderContext::RecreateSwapChain()
{
	_swapChain->RecreateSwapChain();
	for (auto& resize : _resizeCallbacks)
	{
		resize(*_swapChain);
	}
}

void RenderContext::Begin()
{
	// Acquire swap chain image and wait
	AcquireSwapChainAndResetFence(*_swapChain);

	auto& currentFrame = GetCurrentFrame();

	// Allocate command buffers (from CommandPool)
	auto& commandBuffer = _commandPool->RequestCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	auto& computeBuffer = _computeCommandPool->RequestCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

	// Set command buffers to RenderFrame
	currentFrame.SetCommandBuffer(&commandBuffer);
	currentFrame.SetComputeCommandBuffer(&computeBuffer);

	// Reset current frame (Descriptor pool, Command buffers)
	currentFrame.Reset();

	// Begin command buffers
	commandBuffer.BeginCommandBuffer();
	computeBuffer.BeginCommandBuffer();

	if (_bindlessTextureManager)
		_bindlessTextureManager->UpdateDescriptorSet();

	if (_materialManager)
	{
		// Update material buffers if dirty
		_materialManager->RefreshDirtyMaterials();
		currentFrame.UpdateMaterialBuffer(*_materialManager);
	}
}

void RenderContext::Submit()
{
	auto& currentFrame = GetCurrentFrame();
	auto& commandBuffer = currentFrame.GetCommandBuffer();
	auto& computeBuffer = currentFrame.GetComputeCommandBuffer();

	// End command buffers
	commandBuffer.EndCommandBuffer();
	computeBuffer.EndCommandBuffer();

	// Graphics queue submit
	u32 waitSemaphoreCount = 1;

	bool waitForComputeSemaphore = _lastComputeSemaphoreValue > 0;
	if (waitForComputeSemaphore)
		waitSemaphoreCount++;

	bool waitForTimelineSemaphore = FrameCounter::GetFrameNumber() >= _maxFramesInFlight;
	if (waitForTimelineSemaphore)
		waitSemaphoreCount++;

	VkSemaphore waitSemaphores[] =
	{
		currentFrame.GetImageAvailableSemaphore(),
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
		currentFrame.GetRenderFinishedSemaphore(),
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

	// Compute queue submit
	SubmitComputeBuffer();

	// Present
	EndFrame(signalSemaphores);
}

void RenderContext::SubmitComputeBuffer()
{
	auto& currentFrame = GetCurrentFrame();
	auto& computeBuffer = currentFrame.GetComputeCommandBuffer();

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
	auto device = _device.GetDevice();

	// Create timeline semaphores
	VkSemaphoreTypeCreateInfo semaphoreTypeInfo{};
	semaphoreTypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
	semaphoreTypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;

	VkSemaphoreCreateInfo semaphoreInfo{};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreInfo.pNext = &semaphoreTypeInfo;

	if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &_graphicsSemaphore) != VK_SUCCESS ||
		vkCreateSemaphore(device, &semaphoreInfo, nullptr, &_computeSemaphore) != VK_SUCCESS)
	{
		throw runtime_error("Failed to create timeline semaphores!");
	}
}

void RenderContext::AcquireSwapChainAndResetFence(SwapChain& swapChain)
{
	auto device = _device.GetDevice();
	auto& currentFrame = GetCurrentFrame();

	// Wait for GPU work completion (Timeline semaphores)
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

	// Acquire swap chain image
	auto swapChainHandle = swapChain.GetSwapChain();
	VkResult result = vkAcquireNextImageKHR(
		device, swapChainHandle, UINT64_MAX,
		currentFrame.GetImageAvailableSemaphore(),
		VK_NULL_HANDLE, &_imageIndex);

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