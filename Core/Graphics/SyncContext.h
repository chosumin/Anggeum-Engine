#pragma once

namespace Core
{
    enum class QueueType;
    class Device;

    // Owns per-queue timeline semaphores, per-queue timeline values,
    // and per-frame timeline snapshots used for frame slot reuse.
    class SyncContext
    {
    public:
        struct FrameTimelineSnapshot
        {
            u64 graphicsValue = 0;
            u64 computeValue = 0;
            bool valid = false;
        };

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
        void RecordFrameSnapshot(u32 frameIndex);
        const FrameTimelineSnapshot& GetFrameSnapshot(u32 frameIndex) const { return _frameSnapshots[frameIndex]; }

        // Direct handle accessors (for RenderContext internal use)
        VkSemaphore GetGraphicsSemaphore() const { return _graphicsSemaphore; }
        VkSemaphore GetComputeSemaphore() const { return _computeSemaphore; }

    private:
        Device& _device;

        VkSemaphore _graphicsSemaphore = VK_NULL_HANDLE;
        VkSemaphore _computeSemaphore = VK_NULL_HANDLE;

        u64 _graphicsSemaphoreValue = 0;
        u64 _computeSemaphoreValue = 0;

        array<FrameTimelineSnapshot, MAX_FRAMES_IN_FLIGHT> _frameSnapshots;
    };
}
