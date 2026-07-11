#pragma once

namespace Core
{
    enum class QueueType
    {
        Graphics,
        Compute
    };

    class SubmitInfo
    {
    public:
        SubmitInfo(QueueType queueType, VkCommandBuffer commandBuffer)
            : _queueType(queueType), _commandBuffer(commandBuffer) {}

        QueueType GetQueueType() const { return _queueType; }

        void AddWaitSemaphore(VkSemaphore semaphore, VkPipelineStageFlags stage, uint64_t value = 0)
        {
            _waitSemaphores.push_back(semaphore);
            _waitStages.push_back(stage);
            _waitValues.push_back(value);
        }

        void AddSignalSemaphore(VkSemaphore semaphore, uint64_t value = 0)
        {
            _signalSemaphores.push_back(semaphore);
            _signalValues.push_back(value);
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

        std::vector<VkSemaphore>          _waitSemaphores;
        std::vector<VkPipelineStageFlags> _waitStages;
        std::vector<uint64_t>             _waitValues;
        std::vector<VkSemaphore>          _signalSemaphores;
        std::vector<uint64_t>             _signalValues;

        VkTimelineSemaphoreSubmitInfo _timelineInfo{};
        VkSubmitInfo                  _submitInfo{};
    };
}
