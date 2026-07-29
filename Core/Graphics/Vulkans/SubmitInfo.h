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

            _waitSemaphores.push_back(_syncContext->GetSemaphore(queueType));
            _waitStages.push_back(stage);
            _waitValues.push_back(v);
        }

        // Timeline semaphore signal on given queue's timeline (acquires next value)
        void AddSignalSemaphore(QueueType queueType)
        {
            uint64_t v = _syncContext->AcquireNextValue(queueType);

            _signalSemaphores.push_back(_syncContext->GetSemaphore(queueType));
            _signalValues.push_back(v);
        }

        // Timeline semaphore wait at an explicit value, for timelines that are not
        // tied to a queue's own (e.g. the resource-init timeline).
        void AddTimelineWaitSemaphore(VkSemaphore semaphore, uint64_t value,
            VkPipelineStageFlags stage)
        {
            _waitSemaphores.push_back(semaphore);
            _waitStages.push_back(stage);
            _waitValues.push_back(value);
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

	// Everything needed to submit one frame's work: the SubmitInfos collected from
    // the passes, the VkSubmitInfos built from them (grouped per queue), and the
    // frame's swapchain semaphores.
	struct FrameSubmission
	{
        // Collected from the passes as they record, submitted at end of frame.
        deque<SubmitInfo> submitInfos;

        // Persistent VkSubmitInfo: the structs point into the SubmitInfo objects in 
        // submitInfos, and this keeps their lifetime obvious at the call site.
        vector<VkSubmitInfo> submitScratch;

        VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
        VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    };
}
