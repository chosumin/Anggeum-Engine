#include "stdafx.h"
#include "SyncContext.h"
#include "Vulkans/Device.h"
#include "Vulkans/SubmitInfo.h"

using namespace Core;

SyncContext::SyncContext(Device& device)
    : _device(device)
{
    auto device_ = _device.GetDevice();

    VkSemaphoreTypeCreateInfo semaphoreTypeInfo{};
    semaphoreTypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    semaphoreTypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreInfo.pNext = &semaphoreTypeInfo;

    if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &_graphicsSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &_computeSemaphore) != VK_SUCCESS)
    {
        throw runtime_error("Failed to create timeline semaphores!");
    }
}

SyncContext::~SyncContext()
{
    auto device_ = _device.GetDevice();
    if (_graphicsSemaphore != VK_NULL_HANDLE)
        vkDestroySemaphore(device_, _graphicsSemaphore, nullptr);
    if (_computeSemaphore != VK_NULL_HANDLE)
        vkDestroySemaphore(device_, _computeSemaphore, nullptr);
}

VkSemaphore SyncContext::GetSemaphore(QueueType queueType) const
{
    return (queueType == QueueType::Compute) ? _computeSemaphore : _graphicsSemaphore;
}

u64 SyncContext::GetCurrentValue(QueueType queueType) const
{
    return (queueType == QueueType::Compute) ? _computeSemaphoreValue : _graphicsSemaphoreValue;
}

u64 SyncContext::AcquireNextValue(QueueType queueType)
{
    return (queueType == QueueType::Compute) ? ++_computeSemaphoreValue : ++_graphicsSemaphoreValue;
}

void SyncContext::RecordFrameSnapshot(FrameTimelineSnapshot& snapshot)
{
    snapshot.graphicsValue = _graphicsSemaphoreValue;
    snapshot.computeValue = _computeSemaphoreValue;
    snapshot.valid = true;
}

void SyncContext::SubmitToQueues(std::deque<SubmitInfo>& submitInfos,
    VkSemaphore imageAvailable,
    VkSemaphore renderFinished)
{
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

    // Graphics queue submission
    if (!graphicsSubmits.empty())
    {
        // Add imageAvailable wait to first graphics submit info
        auto& firstInfo = submitInfos.front();
        if (firstInfo.GetQueueType() == QueueType::Graphics)
        {
            firstInfo.AddWaitSemaphore(
                imageAvailable,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0);
        }

        // Find last graphics submit info and add signal semaphores
        for (auto it = submitInfos.rbegin(); it != submitInfos.rend(); ++it)
        {
            if (it->GetQueueType() == QueueType::Graphics)
            {
                it->AddSignalSemaphore(renderFinished, 0);
                it->AddSignalSemaphore(
                    _graphicsSemaphore,
                    AcquireNextValue(QueueType::Graphics));
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
    }

    // Compute queue submission
    if (!computeSubmits.empty())
    {
        // Last compute submit signals compute timeline
        for (auto it = submitInfos.rbegin(); it != submitInfos.rend(); ++it)
        {
            if (it->GetQueueType() == QueueType::Compute)
            {
                it->AddSignalSemaphore(
                    _computeSemaphore,
                    AcquireNextValue(QueueType::Compute));
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
    }
}
