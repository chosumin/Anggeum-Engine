#include "stdafx.h"
#include "SyncContext.h"
#include "Vulkans/CommandPool.h"
#include "Vulkans/CommandBuffer.h"
#include "Vulkans/Device.h"
#include "Vulkans/SubmitInfo.h"

using namespace Core;

SyncContext::SyncContext(Device& device)
    : _device(device)
{
    auto device_ = _device.GetDevice();

    const auto& families = _device.GetQueueFamilyIndices();
    vkGetDeviceQueue(device_, families.GraphicsFamily.value(), 0, &_graphicsQueue);
    vkGetDeviceQueue(device_, families.ComputeFamily.value(), 0, &_computeQueue);
    vkGetDeviceQueue(device_, families.PresentFamily.value(), 0, &_presentQueue);
    vkGetDeviceQueue(device_, families.TransferFamily.value(), 0, &_transferQueue);

    VkSemaphoreTypeCreateInfo semaphoreTypeInfo{};
    semaphoreTypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    semaphoreTypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreInfo.pNext = &semaphoreTypeInfo;

    if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &_graphicsSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &_computeSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &_resourceSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &_transferSemaphore) != VK_SUCCESS)
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
    if (_resourceSemaphore != VK_NULL_HANDLE)
        vkDestroySemaphore(device_, _resourceSemaphore, nullptr);
    if (_transferSemaphore != VK_NULL_HANDLE)
        vkDestroySemaphore(device_, _transferSemaphore, nullptr);
}

VkSemaphore SyncContext::GetSemaphore(QueueType queueType) const
{
    switch (queueType)
    {
    case QueueType::Compute:  return _computeSemaphore;
    case QueueType::Transfer: return _transferSemaphore;
    default:                  return _graphicsSemaphore;
    }
}

u64 SyncContext::GetCurrentValue(QueueType queueType) const
{
    switch (queueType)
    {
    case QueueType::Compute:  return _computeSemaphoreValue;
    case QueueType::Transfer: return _transferSemaphoreValue;
    default:                  return _graphicsSemaphoreValue;
    }
}

u64 SyncContext::QueryCompletedValue(QueueType queueType) const
{
    u64 value = 0;
    vkGetSemaphoreCounterValue(_device.GetDevice(), GetSemaphore(queueType), &value);
    return value;
}

u64 SyncContext::GetCachedCompletedValue(QueueType queueType) const
{
    switch (queueType)
    {
    case QueueType::Compute:  return _computeCompletedCache;
    case QueueType::Transfer: return _transferCompletedCache;
    default:                  return _graphicsCompletedCache;
    }
}

void SyncContext::RefreshCompletedCache()
{
    _graphicsCompletedCache = QueryCompletedValue(QueueType::Graphics);
    _computeCompletedCache = QueryCompletedValue(QueueType::Compute);
    _transferCompletedCache = QueryCompletedValue(QueueType::Transfer);
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

void SyncContext::SubmitResourceInit(CommandBuffer& commandBuffer)
{
    u64 signalValue = ++_resourceSemaphoreValue;

    VkTimelineSemaphoreSubmitInfo timelineInfo{};
    timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineInfo.signalSemaphoreValueCount = 1;
    timelineInfo.pSignalSemaphoreValues = &signalValue;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = &timelineInfo;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer.GetHandle();
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &_resourceSemaphore;

    if (vkQueueSubmit(_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
        throw runtime_error("failed to submit resource init commands!");

    _pendingResourceWait = signalValue;

    _pendingInitBuffers.push_back(&commandBuffer);
}

VkResult SyncContext::Present(const VkPresentInfoKHR& presentInfo)
{
    return vkQueuePresentKHR(_presentQueue, &presentInfo);
}

u64 SyncContext::SubmitTransfer(CommandBuffer& primary,
    const vector<CommandBuffer*>& secondaries)
{
    u64 signalValue = ++_transferSemaphoreValue;

    VkTimelineSemaphoreSubmitInfo timelineInfo{};
    timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineInfo.signalSemaphoreValueCount = 1;
    timelineInfo.pSignalSemaphoreValues = &signalValue;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = &timelineInfo;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &primary.GetHandle();
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &_transferSemaphore;

    if (vkQueueSubmit(_transferQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
        throw runtime_error("failed to submit transfer commands!");

    // Everything this submission executes recycles when the GPU passes it.
    primary.MarkSubmitted(signalValue);
    for (CommandBuffer* secondary : secondaries)
        secondary->MarkSubmitted(signalValue);

    // Monotonic: a later submit's value covers every earlier one.
    _pendingTransferWait = signalValue;
    return signalValue;
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

    // Work that went out ahead of this frame (resource init on graphics,
    // uploads on transfer) gates the first submit of each queue: a pass may
    // sample a target it transitioned or read a buffer it filled, and the
    // compute queue has no implicit ordering against the other queues at all.
    auto gateBothQueues = [&](VkSemaphore semaphore, u64& pendingWait)
    {
        if (pendingWait == 0)
            return;

        bool graphicsGated = false;
        bool computeGated = false;

        for (auto& info : submitInfos)
        {
            bool& gated = (info.GetQueueType() == QueueType::Compute)
                ? computeGated : graphicsGated;

            if (gated)
                continue;

            info.AddTimelineWaitSemaphore(semaphore, pendingWait,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
            gated = true;

            if (graphicsGated && computeGated)
                break;
        }

        pendingWait = 0;
    };

    gateBothQueues(_resourceSemaphore, _pendingResourceWait);
    gateBothQueues(_transferSemaphore, _pendingTransferWait);

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
            ? _computeQueue
            : _graphicsQueue;

        if (vkQueueSubmit(queue,
            static_cast<uint32_t>(scratch.size()),
            scratch.data(), VK_NULL_HANDLE) != VK_SUCCESS)
        {
            throw runtime_error("failed to submit command buffers!");
        }

        index = runEnd;
    }

    // The submitter stamps what it submitted: each buffer retires on its
    // queue's end-of-frame value (a queue's last signal orders after all of
    // the frame's earlier submissions on that queue). The resource-init
    // primaries rode the graphics queue ahead of the frame, so the same
    // graphics value covers them.
    const u64 graphicsValue = GetCurrentValue(QueueType::Graphics);
    const u64 computeValue = GetCurrentValue(QueueType::Compute);

    for (auto& info : submitInfos)
    {
        info.GetCommandBuffer().MarkSubmitted(
            info.GetQueueType() == QueueType::Compute ? computeValue : graphicsValue);
    }

    for (CommandBuffer* initBuffer : _pendingInitBuffers)
        initBuffer->MarkSubmitted(graphicsValue);
    _pendingInitBuffers.clear();
}
