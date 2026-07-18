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

void SyncContext::SubmitToQueues(
    deque<SubmitInfo>& submitInfos,
    vector<VkSubmitInfo>& scratch,
    VkSemaphore imageAvailable,
    VkSemaphore renderFinished)
{
    if (submitInfos.empty())
        return;

    // Inject frame-level semaphores before building.

    // imageAvailable wait -> first graphics submit
    for (auto& info : submitInfos)
    {
        if (info.GetQueueType() == QueueType::Graphics)
        {
            info.AddBinaryWaitSemaphore(
                imageAvailable,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
            break;
        }
    }

    // renderFinished binary + graphics timeline signal -> last graphics submit
    for (auto it = submitInfos.rbegin(); it != submitInfos.rend(); ++it)
    {
        if (it->GetQueueType() == QueueType::Graphics)
        {
            it->AddBinarySignalSemaphore(renderFinished);
            it->AddSignalSemaphore(QueueType::Graphics);
            break;
        }
    }

    // compute timeline signal -> last compute submit
    for (auto it = submitInfos.rbegin(); it != submitInfos.rend(); ++it)
    {
        if (it->GetQueueType() == QueueType::Compute)
        {
            it->AddSignalSemaphore(QueueType::Compute);
            break;
        }
    }

    // Submit one run of consecutive same-queue passes at a time, in recorded
    // order.
    size_t index = 0;
    while (index < submitInfos.size())
    {
        const QueueType queueType = submitInfos[index].GetQueueType();

        scratch.clear();
        size_t runEnd = index;
        for (; runEnd < submitInfos.size() && submitInfos[runEnd].GetQueueType() == queueType; ++runEnd)
            scratch.push_back(submitInfos[runEnd].Build());

        VkQueue queue = (queueType == QueueType::Compute)
            ? _device.GetComputeQueue()
            : _device.GetGraphicsQueue();

        if (vkQueueSubmit(queue,
            static_cast<uint32_t>(scratch.size()),
            scratch.data(), VK_NULL_HANDLE) != VK_SUCCESS)
        {
            throw runtime_error("failed to submit command buffers!");
        }

        index = runEnd;
    }
}
