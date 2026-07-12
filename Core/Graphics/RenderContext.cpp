#include "stdafx.h"
#include "RenderContext.h"
#include "RenderFrame.h"
#include "Vulkans/SubmitInfo.h"
#include "Foundation/Scene.h"
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

	_meshBufferManager = make_unique<MeshBufferManager>(_device);
	_materialManager = make_unique<MaterialManager>();

	auto queueFamilyIndices = device.GetQueueFamilyIndices();

	// Create command pools (owned by RenderContext)
	_commandPool = new CommandPool(device, queueFamilyIndices.GraphicsFamily.value());
	_computeCommandPool = new CommandPool(device, queueFamilyIndices.ComputeFamily.value());

	CreateRenderFrames();
	CreateSyncObjects();	

	if (_bindlessTextureManager)
	{
		_bindlessTextureManager->Initialize();
		cout << "Bindless texture system initialized with "
			<< _bindlessTextureManager->GetMaxTextures() << " slots" << endl;
	}
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

		_frames[i]->SetMeshBufferManager(_meshBufferManager.get());
		_frames[i]->SetMaterialManager(_materialManager.get());
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
	// Acquire swap chain image and wait
	AcquireSwapChainAndResetFence(*_swapChain);

	auto& currentFrame = GetCurrentFrame();

	uint32_t prevIndex = (_currentFrame + MAX_FRAMES_IN_FLIGHT - 1) % MAX_FRAMES_IN_FLIGHT;
	_previousFrameDepth = _frameDepthBuffers[prevIndex];
	currentFrame.SetPreviousDepthBuffer(_previousFrameDepth);

	// Reset current frame (Descriptor pool, Command buffers, Submit infos)
	currentFrame.Reset();

	if (_bindlessTextureManager)
		_bindlessTextureManager->UpdateDescriptorSet();

	_materialManager->RefreshDirtyMaterials();

	// Initialize batches once per frame
	currentFrame.InitializeBatches(scene, extents);
}

void RenderContext::Submit()
{
	auto& currentFrame = GetCurrentFrame();

	auto currentResolvedDepth = currentFrame.GetRenderTarget("ResolvedDepth");

	if (currentResolvedDepth)
		_frameDepthBuffers[_currentFrame] = currentResolvedDepth;
	else
	{
		auto currentDepth = currentFrame.GetRenderTarget("MainDepth");
		_frameDepthBuffers[_currentFrame] = currentDepth;
	}

	auto& submitInfos = currentFrame.GetSubmitInfos();

	// Separate submit infos by queue type
	std::vector<VkSubmitInfo> graphicsSubmits;
	std::vector<VkSubmitInfo> computeSubmits;

	for (auto& info : submitInfos)
	{
		const VkSubmitInfo& vkInfo = info.Build();
		if (info.GetQueueType() == QueueType::Graphics)
			graphicsSubmits.push_back(vkInfo);
		else
			computeSubmits.push_back(vkInfo);
	}

	// Inject frame-level semaphores into first/last graphics submits
	// First graphics submit waits on imageAvailable + timeline
	// Last graphics submit signals renderFinished + timeline
	if (!graphicsSubmits.empty())
	{
		// Add imageAvailable wait to first graphics submit info
		auto& firstInfo = submitInfos.front();
		if (firstInfo.GetQueueType() == QueueType::Graphics)
		{
			firstInfo.AddWaitSemaphore(
				currentFrame.GetImageAvailableSemaphore(),
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0);
		}

		// Find last graphics submit info and add signal semaphores
		for (auto it = submitInfos.rbegin(); it != submitInfos.rend(); ++it)
		{
			if (it->GetQueueType() == QueueType::Graphics)
			{
				it->AddSignalSemaphore(
					currentFrame.GetRenderFinishedSemaphore(), 0);
				it->AddSignalSemaphore(
					_graphicsSemaphore,
					_graphicsSemaphoreValue + 1);
				break;
			}
		}

		// Rebuild after adding frame-level semaphores
		graphicsSubmits.clear();
		for (auto& info : submitInfos)
		{
			if (info.GetQueueType() == QueueType::Graphics)
				graphicsSubmits.push_back(info.Build());
		}

		if (vkQueueSubmit(_device.GetGraphicsQueue(),
			static_cast<uint32_t>(graphicsSubmits.size()),
			graphicsSubmits.data(), VK_NULL_HANDLE) != VK_SUCCESS)
		{
			throw runtime_error("failed to submit graphics command buffers!");
		}

		++_graphicsSemaphoreValue;
	}

	// Compute queue submit
	if (!computeSubmits.empty())
	{
		// Last compute submit signals current frame's compute timeline
		for (auto it = submitInfos.rbegin(); it != submitInfos.rend(); ++it)
		{
			if (it->GetQueueType() == QueueType::Compute)
			{
				it->AddSignalSemaphore(
					_computeSemaphore,
					_computeSemaphoreValue + 1);
				break;
			}
		}

		// Rebuild compute submits after adding frame-level semaphores
		computeSubmits.clear();
		for (auto& info : submitInfos)
		{
			if (info.GetQueueType() == QueueType::Compute)
				computeSubmits.push_back(info.Build());
		}

		if (vkQueueSubmit(_device.GetComputeQueue(),
			static_cast<uint32_t>(computeSubmits.size()),
			computeSubmits.data(), VK_NULL_HANDLE) != VK_SUCCESS)
		{
			throw runtime_error("failed to submit compute command buffers!");
		}

		++_computeSemaphoreValue;
	}

	// Record this frame's final timeline values so the next reuse of this slot
	// can wait for GPU completion regardless of per-frame increment count.
	_frameSnapshots[_currentFrame] = { _graphicsSemaphoreValue, _computeSemaphoreValue, true };

	// Present
	VkSemaphore renderFinished = currentFrame.GetRenderFinishedSemaphore();
	EndFrame(&renderFinished);
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

	// Wait for the previous use of this frame slot to complete on the GPU.
	const auto& snapshot = _frameSnapshots[_currentFrame];
	if (snapshot.valid)
	{
		u64 waitValues[] = { snapshot.graphicsValue, snapshot.computeValue };
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