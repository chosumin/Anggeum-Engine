#pragma once
#include "Graphics/SyncContext.h"

namespace Core
{
    class SubmitInfo
    {
    public:
        SubmitInfo(QueueType queueType, VkCommandBuffer commandBuffer, SyncContext& syncContext)
            : _queueType(queueType), _commandBuffer(commandBuffer), _syncContext(&syncContext) {}

        QueueType GetQueueType() const { return _queueType; }

        // Timeline semaphore wait on given queue's timeline (uses current value)
        void AddWaitSemaphore(QueueType queueType, VkPipelineStageFlags stage)
        {
            uint64_t v = _syncContext->GetCurrentValue(queueType);
            printf("[SYNC] queue=%d WAIT on %s timeline value=%llu\n",
                (int)_queueType, queueType == QueueType::Compute ? "Compute" : "Graphics",
                (unsigned long long)v);
            _waitSemaphores.push_back(_syncContext->GetSemaphore(queueType));
            _waitStages.push_back(stage);
            _waitValues.push_back(v);
        }

        // Timeline semaphore signal on given queue's timeline (acquires next value)
        void AddSignalSemaphore(QueueType queueType)
        {
            uint64_t v = _syncContext->AcquireNextValue(queueType);
            printf("[SYNC] queue=%d SIGNAL %s timeline value=%llu\n",
                (int)_queueType, queueType == QueueType::Compute ? "Compute" : "Graphics",
                (unsigned long long)v);
            _signalSemaphores.push_back(_syncContext->GetSemaphore(queueType));
            _signalValues.push_back(v);
        }

        // Binary semaphore wait (e.g. swapchain imageAvailable)
        void AddBinaryWaitSemaphore(VkSemaphore semaphore, VkPipelineStageFlags stage)
        {
            _waitSemaphores.push_back(semaphore);
            _waitStages.push_back(stage);
            _waitValues.push_back(0);
        }

        // Binary semaphore signal (e.g. renderFinished for present)
        void AddBinarySignalSemaphore(VkSemaphore semaphore)
        {
            _signalSemaphores.push_back(semaphore);
            _signalValues.push_back(0);
        }

        const VkSubmitInfo& Build()
        {
            _timelineInfo = {};
            _timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
            _timelineInfo.waitSemaphoreValueCount = static_cast<uint32_t>(_waitValues.size());
            _timelineInfo.pWaitSemaphoreValues = _waitValues.data();
            _timelineInfo.signalSemaphoreValueCount = static_cast<uint32_t>(_signalValues.size());
            _timelineInfo.pSignalSemaphoreValues = _signalValues.data();

            _submitInfo = {};
            _submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            _submitInfo.pNext = &_timelineInfo;
            _submitInfo.waitSemaphoreCount = static_cast<uint32_t>(_waitSemaphores.size());
            _submitInfo.pWaitSemaphores = _waitSemaphores.data();
            _submitInfo.pWaitDstStageMask = _waitStages.data();
            _submitInfo.commandBufferCount = 1;
            _submitInfo.pCommandBuffers = &_commandBuffer;
            _submitInfo.signalSemaphoreCount = static_cast<uint32_t>(_signalSemaphores.size());
            _submitInfo.pSignalSemaphores = _signalSemaphores.data();

            return _submitInfo;
        }

    private:
        QueueType _queueType;
        VkCommandBuffer _commandBuffer;
        SyncContext* _syncContext;

        std::vector<VkSemaphore>          _waitSemaphores;
        std::vector<VkPipelineStageFlags> _waitStages;
        std::vector<uint64_t>             _waitValues;
        std::vector<VkSemaphore>          _signalSemaphores;
        std::vector<uint64_t>             _signalValues;

        VkTimelineSemaphoreSubmitInfo _timelineInfo{};
        VkSubmitInfo                  _submitInfo{};
    };
}
