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

void SyncContext::RecordFrameSnapshot(u32 frameIndex)
{
    _frameSnapshots[frameIndex] = { _graphicsSemaphoreValue, _computeSemaphoreValue, true };
}
