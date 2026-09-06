#pragma once

namespace Core
{
	class Device;
	class Buffer;

	// The engine's staging arena: a persistently-mapped, self-owned allocation
	// (no pool involved). Spans are handed out circularly, closed with the
	// timeline value of the submission that consumed them, and
	// reclaimed FIFO once the GPU passes that value - steady-state traffic
	// reuses the same memory instead of allocating per upload.
	//
	// Sized for steady-state traffic, not load spikes: a request that does not
	// fit returns an invalid span and the caller falls back to a dedicated
	// one-shot DEDICATED_HOST allocation.
	class StagingRing
	{
	public:
		struct Span
		{
			Buffer* buffer = nullptr;
			VkDeviceSize offset = 0;
			uint8_t* mapped = nullptr;
			uint64_t id = UINT64_MAX;

			bool IsValid() const { return buffer != nullptr; }
		};

		StagingRing(Device& device, VkDeviceSize capacity);
		~StagingRing();

		// Thread-safe: jobs acquire on worker threads while they record.
		Span Acquire(VkDeviceSize size);

		// Every acquired span must be closed exactly once, with the timeline value that retires it.
		void Close(uint64_t spanId, uint64_t timelineValue);

		// Free the closed-and-completed PREFIX.
		void Reclaim(uint64_t completedTimelineValue);

		// Starts a budget window: resets the requested-bytes tally the shared
		// upload budget is measured against.
		void BeginFrame();

		// Stats. `FrameRequested` counts every byte asked for this window -
		// including failed acquires, so fallback traffic charges the budget too.
		VkDeviceSize GetCapacity() const { return _capacity; }
		VkDeviceSize GetUsed();
		VkDeviceSize GetPeakUsed();
		VkDeviceSize GetFrameRequested();
		uint32_t GetFallbackCount();

	private:
		// Covers optimalBufferCopyOffsetAlignment and texel-block-size
		// requirements of buffer-image copies.
		static constexpr VkDeviceSize ALIGNMENT = 256;

		unique_ptr<Buffer> _buffer;
		uint8_t* _mapped = nullptr;
		VkDeviceSize _capacity = 0;

		// Circular head; `_used` counts every live byte including the padding
		// skipped at the ring's end on wrap, so "fits" is a pure byte check.
		VkDeviceSize _head = 0;
		VkDeviceSize _used = 0;

		// One record per Acquire, FIFO in ring order.
		struct SpanRecord
		{
			VkDeviceSize used;   // span bytes + any wrap padding it caused
			bool closed = false;
			uint64_t value = 0;
		};
		deque<SpanRecord> _records;
		uint64_t _baseId = 0;

		VkDeviceSize _peakUsed = 0;
		VkDeviceSize _frameRequested = 0;
		uint32_t _fallbacks = 0;

		mutex _mutex;
	};
}
