#pragma once

namespace Core
{
    enum class QueueType
    {
        None,
        Graphics,
        Compute
    };

    class Device;
    class SubmitInfo;

    struct FrameTimelineSnapshot
    {
        u64 graphicsValue = 0;
        u64 computeValue = 0;
        bool valid = false;
    };

    // Owns per-queue timeline semaphores, per-queue timeline values,
    // and per-frame timeline snapshots used for frame slot reuse.
    // Also handles queue submission with semaphore injection.
    class SyncContext
    {
    public:
        SyncContext(Device& device);
        ~SyncContext();

        SyncContext(const SyncContext&) = delete;
        SyncContext& operator=(const SyncContext&) = delete;

        // Timeline semaphores per queue
        VkSemaphore GetSemaphore(QueueType queueType) const;

        // Value management
        u64 GetCurrentValue(QueueType queueType) const;
        u64 AcquireNextValue(QueueType queueType);

        // Frame slot snapshots (for reusing a frame slot safely)
        void RecordFrameSnapshot(FrameTimelineSnapshot& snapshot);

        // Queue submission with automatic semaphore injection
        void SubmitToQueues(unordered_map<QueueType, vector<VkSubmitInfo>>& submitOutput,
                           deque<SubmitInfo>& submitInfos,
                           VkSemaphore imageAvailable,
                           VkSemaphore renderFinished);

        // Direct handle accessors (for RenderContext internal use)
        VkSemaphore GetGraphicsSemaphore() const { return _graphicsSemaphore; }
        VkSemaphore GetComputeSemaphore() const { return _computeSemaphore; }

    private:
        Device& _device;

        VkSemaphore _graphicsSemaphore = VK_NULL_HANDLE;
        VkSemaphore _computeSemaphore = VK_NULL_HANDLE;

        u64 _graphicsSemaphoreValue = 0;
        u64 _computeSemaphoreValue = 0;

    };
}
