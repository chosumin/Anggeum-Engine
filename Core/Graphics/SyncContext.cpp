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
    if (submitInfos.empty())
        return;

    // Inject frame-level semaphores before building. The submission itself is
    // done in the exact order the passes were recorded so that the validation
    // layer (which tracks image/buffer queue ownership and layouts in
    // vkQueueSubmit call order) observes producer releases before consumer
    // acquires across the graphics/compute queues.

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

    // Submit consecutive runs of the same queue type in recorded order.
    size_t i = 0;
    while (i < submitInfos.size())
    {
        QueueType queueType = submitInfos[i].GetQueueType();

        std::vector<VkSubmitInfo> batch;
        size_t j = i;
        for (; j < submitInfos.size() && submitInfos[j].GetQueueType() == queueType; ++j)
            batch.push_back(submitInfos[j].Build());

        VkQueue queue = (queueType == QueueType::Compute)
            ? _device.GetComputeQueue()
            : _device.GetGraphicsQueue();

        if (vkQueueSubmit(queue,
            static_cast<uint32_t>(batch.size()),
            batch.data(), VK_NULL_HANDLE) != VK_SUCCESS)
        {
            throw runtime_error("failed to submit command buffers!");
        }

        i = j;
    }
}
