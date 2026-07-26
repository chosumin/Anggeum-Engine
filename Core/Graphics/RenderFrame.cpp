#include "stdafx.h"
#include "RenderFrame.h"
#include "Vulkans/UniformBuffer.h"
#include "Vulkans/Buffer.h"
#include "Vulkans/CommandBuffer.h"
#include "Vulkans/SubmitInfo.h"

using namespace Core;

namespace
{
	// Shared sentinel for temp frames that draw without GPU-driven managers; all of
	// its managers stay null, so the accessors report "no manager" as before.
	GDRManagers& EmptyManagers()
	{
		static GDRManagers empty;
		return empty;
	}
}

RenderFrame::RenderFrame(Device& device, GDRManagers& managers)
	: _device(device)
	, _gdrManagers(managers)
	, _resources(device)
{
	CreateSyncObjects();

	_renderExecutor = make_unique<RenderExecutor>(device, *this);
}

RenderFrame::RenderFrame(Device& device)
	: RenderFrame(device, EmptyManagers())
{
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

	// Reset culler usage tracking for this frame
	_renderExecutor->ResetFrame();

	// Recycles this frame's descriptor pool and clears transient selectors.
	_resources.Reset();
}

DescriptorSetResources* RenderFrame::GetBindlessResources()
{
	if (!HasBindlessSupport())
		return nullptr;

	_bindlessResources.descriptorSet = _gdrManagers.Bindless->GetDescriptorSet();
	_bindlessResources.setIndex = static_cast<uint32_t>(DescriptorSetType::Bindless);
	return &_bindlessResources;
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