#pragma once

namespace Core
{
    enum class QueueType
    {
        None,
        Graphics,
        Compute,
        Transfer
    };

    class Device;
    class SubmitInfo;
    class CommandPool;
    class CommandBuffer;

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

        // The GPU-side progress of a queue's timeline: every submission whose
        // value is <= this has fully executed.
        u64 GetCompletedValue(QueueType queueType) const;

        void RefreshCompletedCache();

        // Frame slot snapshots (for reusing a frame slot safely)
        void RecordFrameSnapshot(FrameTimelineSnapshot& snapshot);

        // Submits this frame's resource-init work. It signals its own timeline
        // rather than a queue timeline, so the next SubmitToQueues can gate the
        // first submit of *both* queues on it.
        void SubmitResourceInit(CommandBuffer& commandBuffer);

        // Submits an upload batch on the transfer queue, signalling the
        // transfer timeline; returns the signalled value.
        u64 SubmitTransfer(CommandBuffer& primary,
            const vector<CommandBuffer*>& secondaries);

        // Injects the frame-level semaphores and submits the frame
        void SubmitToQueues(deque<SubmitInfo>& submitInfos,
                            vector<VkSubmitInfo>& scratch,
                            VkSemaphore imageAvailable,
                            VkSemaphore renderFinished);

        // Presentation is a queue operation too, so it goes through the
        // submission authority like every submit.
        VkResult Present(const VkPresentInfoKHR& presentInfo);

        // Direct handle accessors (for RenderContext internal use)
        VkSemaphore GetGraphicsSemaphore() const { return _graphicsSemaphore; }
        VkSemaphore GetComputeSemaphore() const { return _computeSemaphore; }

        // For third-party init only (ImGui's backend wants the raw handle);
        // engine code submits through this class, never through the handle.
        VkQueue GetGraphicsQueueForExternalInit() const { return _graphicsQueue; }
    private:
        // Resource-init buffers submitted since the last SubmitToQueues,
        // awaiting their end-of-frame graphics stamp.
        vector<CommandBuffer*> _pendingInitBuffers;

        Device& _device;

        // The queue handles live here, not on Device: submission (and its
        // timeline bookkeeping) has exactly one owner.
        VkQueue _graphicsQueue = VK_NULL_HANDLE;
        VkQueue _computeQueue = VK_NULL_HANDLE;
        VkQueue _presentQueue = VK_NULL_HANDLE;
        VkQueue _transferQueue = VK_NULL_HANDLE;

        VkSemaphore _graphicsSemaphore = VK_NULL_HANDLE;
        VkSemaphore _computeSemaphore = VK_NULL_HANDLE;
        VkSemaphore _resourceSemaphore = VK_NULL_HANDLE;
        VkSemaphore _transferSemaphore = VK_NULL_HANDLE;

        u64 _graphicsSemaphoreValue = 0;
        u64 _computeSemaphoreValue = 0;
        u64 _resourceSemaphoreValue = 0;
        u64 _transferSemaphoreValue = 0;

        // Values the next SubmitToQueues must wait on, or 0 when nothing was
        // submitted for this frame. Cleared once the wait is injected.
        u64 _pendingResourceWait = 0;
        u64 _pendingTransferWait = 0;

        // Per-frame snapshot of the driver completed values, readable anywhere.
        atomic<u64> _graphicsCompletedCache{ 0 };
        atomic<u64> _computeCompletedCache{ 0 };
        atomic<u64> _transferCompletedCache{ 0 };
    };
}
