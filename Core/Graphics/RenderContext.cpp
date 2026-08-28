#include "stdafx.h"
#include "RenderContext.h"
#include "RenderFrame.h"
#include "Vulkans/SubmitInfo.h"
#include "Foundation/Scene.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/CommandPool.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/BindlessTextureManager.h"
#include "RendererBatch.h"
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

RenderContext::RenderContext(Device& device, ResourceManager& resourceManager,
	RenderScene& renderScene, SyncContext& syncContext)
	: _device(device)
	, _resourceManager(resourceManager)
	, _renderScene(renderScene)
	, _syncContext(syncContext)
{
	_swapChain = new SwapChain(device);

	auto queueFamilyIndices = device.GetQueueFamilyIndices();

	// Create command pools (owned by RenderContext)
	_commandPool = new CommandPool(device, queueFamilyIndices.GraphicsFamily.value());
	_computeCommandPool = new CommandPool(device, queueFamilyIndices.ComputeFamily.value());

	_queueTimer = make_unique<GpuQueueTimer>(device);

	CreateRenderFrames();
}

RenderContext::~RenderContext()
{
	// Clean up frames first: their per-frame Cullers hold references to the shared
	// RendererBatch, so the managers (destroyed with _gpuManagers after this dtor
	// body) must outlive them.
	_frames.clear();

	// The sync context is Engine-owned and outlives this frame manager.
	_queueTimer.reset();

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
		_frames[i] = make_unique<RenderFrame>(_device, _resourceManager, _renderScene);
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

void RenderContext::Begin(Scene& scene, VkExtent2D extents)
{
	// The frame slot DERIVES from the engine-owned frame counter 
	// (Engine advances it once per loop, after Submit).
	_currentFrame = uint32_t(FrameCounter::GetFrameNumber() % MAX_FRAMES_IN_FLIGHT);

	// Acquire swap chain image and wait
	AcquireSwapChainAndResetFence(*_swapChain);

	// The wait above guarantees this slot's previous frame finished on the GPU, so
	// the timestamps it wrote are readable now.
	auto timings = _queueTimer->Resolve(_currentFrame);
	if (timings.valid)
		_lastQueueTimings = timings;

	auto& currentFrame = GetCurrentFrame();

	uint32_t prevIndex = (_currentFrame + MAX_FRAMES_IN_FLIGHT - 1) % MAX_FRAMES_IN_FLIGHT;
	_previousFrameDepth = _frameDepthBuffers[prevIndex];
	currentFrame.GetResources().SetPreviousDepthBuffer(_previousFrameDepth);

	// Reset current frame (Descriptor pool, Command buffers, Submit infos)
	currentFrame.Reset();
}

void RenderContext::Submit()
{
	auto& currentFrame = GetCurrentFrame();
	auto& frameResources = currentFrame.GetResources();

	auto currentResolvedDepth = frameResources.GetRenderTarget("ResolvedDepth");

	if (currentResolvedDepth.IsValid())
		_frameDepthBuffers[_currentFrame] = currentResolvedDepth;
	else
	{
		auto currentDepth = frameResources.GetRenderTarget("MainDepth");
		_frameDepthBuffers[_currentFrame] = currentDepth;
	}

	auto& submission = currentFrame.GetSubmission();

	auto submitStart = std::chrono::steady_clock::now();

	// Resource-init work the passes queued while recording (initial layout
	// transitions, initial buffer fills) goes out first, as one command buffer on
	// the graphics queue. SubmitToQueues gates the frame's first submit per queue
	// on the timeline value it signals.
	if (frameResources.HasPendingInit())
	{
		auto& initCommandBuffer = RequestCommandBuffer();
		initCommandBuffer.BeginCommandBuffer(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

		frameResources.ExecutePendingInit(initCommandBuffer);

		initCommandBuffer.EndCommandBuffer();

		_syncContext.SubmitResourceInit(initCommandBuffer.GetHandle());
	}

	// Submit all queues with semaphore injection
	_syncContext.SubmitToQueues(
		submission.submitInfos,
		submission.submitScratch,
		submission.imageAvailableSemaphore,
		submission.renderFinishedSemaphore);

	// Record this frame's final timeline values
	_syncContext.RecordFrameSnapshot(_frameSnapshots[_currentFrame]);

	auto presentStart = std::chrono::steady_clock::now();

	VkSemaphore renderFinished = submission.renderFinishedSemaphore;
	EndFrame(&renderFinished);

	auto presentEnd = std::chrono::steady_clock::now();

	_lastQueueSubmitMs = std::chrono::duration<double, std::milli>(presentStart - submitStart).count();
	_lastPresentMs = std::chrono::duration<double, std::milli>(presentEnd - presentStart).count();
}

CommandBuffer& RenderContext::RequestCommandBuffer()
{
	auto& cmd = _commandPool->RequestCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	return cmd;
}

CommandBuffer& RenderContext::RequestComputeCommandBuffer()
{
	auto& cmd = _computeCommandPool->RequestCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	return cmd;
}

SwapChain& RenderContext::GetSwapChain() const
{
	return *_swapChain;
}

VkExtent2D RenderContext::GetSurfaceExtent() const
{
	return _swapChain->GetSwapChainExtent();
}

void RenderContext::AcquireSwapChainAndResetFence(SwapChain& swapChain)
{
	auto device = _device.GetDevice();
	auto& currentFrame = GetCurrentFrame();

	// Wait for the previous use of this frame slot to complete on the GPU.
	const auto& snapshot = _frameSnapshots[_currentFrame];
	if (snapshot.valid)
	{
		u64 waitValues[] = { snapshot.graphicsValue, snapshot.computeValue };
		VkSemaphore waitSemaphores[] = { _syncContext.GetGraphicsSemaphore(), _syncContext.GetComputeSemaphore() };

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
		currentFrame.GetSubmission().imageAvailableSemaphore,
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

	VkResult result = _syncContext.Present(presentInfo);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || Window::FramebufferResized)
	{
		Window::FramebufferResized = false;
		RecreateSwapChain();
	}
	else if (result != VK_SUCCESS)
		throw runtime_error("failed to present swap chain image!");

}
