#include "stdafx.h"
#include "RenderFrame.h"
#include "Vulkans/UniformBuffer.h"
#include "Vulkans/Buffer.h"
#include "Vulkans/CommandBuffer.h"
#include "Vulkans/SubmitInfo.h"
#include "Vulkans/Shader.h"
#include "Vulkans/Pipeline.h"
#include "Vulkans/DescriptorSetBuilder.h"
#include "RendererBatch.h"
#include "MeshBufferManager.h"
#include "MaterialManager.h"

using namespace Core;

RenderFrame::RenderFrame(Device& device, RenderScene& renderScene)
	: _device(device)
	, _renderScene(renderScene)
	, _resources(device)
{
	CreateSyncObjects();
}

RenderFrame::~RenderFrame()
{
	if (_submission.imageAvailableSemaphore != VK_NULL_HANDLE)
	{
		vkDestroySemaphore(_device.GetDevice(), _submission.imageAvailableSemaphore, nullptr);
	}
	if (_submission.renderFinishedSemaphore != VK_NULL_HANDLE)
	{
		vkDestroySemaphore(_device.GetDevice(), _submission.renderFinishedSemaphore, nullptr);
	}
}

void RenderFrame::Reset()
{
	// submitScratch points into submitInfos, so both are cleared together here.
	_submission.submitInfos.clear();
	_submission.submitScratch.clear();

	// Recycles this frame's descriptor pool and clears transient selectors.
	_resources.Reset();
}

void RenderFrame::CreateSyncObjects()
{
	VkSemaphoreCreateInfo semaphoreInfo{};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	if (vkCreateSemaphore(_device.GetDevice(), &semaphoreInfo, nullptr, &_submission.imageAvailableSemaphore) != VK_SUCCESS ||
		vkCreateSemaphore(_device.GetDevice(), &semaphoreInfo, nullptr, &_submission.renderFinishedSemaphore) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create semaphores for a frame!");
	}
}

SubmitInfo& RenderFrame::AddSubmitInfo(QueueType queueType, VkCommandBuffer commandBuffer, SyncContext& syncContext)
{
	_submission.submitInfos.emplace_back(queueType, commandBuffer, syncContext);
	return _submission.submitInfos.back();
}
