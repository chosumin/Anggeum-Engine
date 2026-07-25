#include "stdafx.h"
#include "RenderFrame.h"
#include "Material.h"
#include "Vulkans/UniformBuffer.h"
#include "Vulkans/Buffer.h"
#include "Vulkans/DescriptorPool.h"
#include "Vulkans/Shader.h"
#include "Vulkans/Framebuffer.h"
#include "Vulkans/DescriptorSetBuilder.h"
#include "Vulkans/CommandBuffer.h"
#include "Vulkans/CommandPool.h"
#include "Vulkans/SubmitInfo.h"
#include "ResourceCache.h"
#include "TransferJob.h"
#include "Foundation/Scene.h"
#include "Foundation/Entity.h"
#include "Components/Mesh.h"
#include "Components/Transform.h"

using namespace Core;

RenderFrame::RenderFrame(Device& device, BindlessTextureManager* bindlessManager)
	: _device(device)
	, _bindlessTextureManager(bindlessManager)
	, _resources(device)
{
	CreateSyncObjects();

	_renderExecutor = make_unique<RenderExecutor>(device, *this);
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

	_bindlessResources.descriptorSet = _bindlessTextureManager->GetDescriptorSet();
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

void RenderFrame::InitializeBatches(Scene& scene, VkExtent2D extents)
{
	_renderExecutor->InitializeBatches(scene, extents);
}

SubmitInfo& RenderFrame::AddSubmitInfo(QueueType queueType, VkCommandBuffer commandBuffer, SyncContext& syncContext)
{
	_submission.submitInfos.emplace_back(queueType, commandBuffer, syncContext);
	return _submission.submitInfos.back();
}