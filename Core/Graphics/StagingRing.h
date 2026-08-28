#pragma once

namespace Core
{
	class Device;
	class Buffer;

	// Persistently-mapped staging arena for uploads. Spans are handed out
	// circularly and reclaimed when the timeline they were stamped with passes
	// their value, so steady-state traffic reuses the same memory instead of
	// allocating a fresh staging buffer per upload.
	//
	// Two timelines retire spans: transfer submissions (upload jobs) and frame
	// slots (spans consumed by frame-graph copies, safe once Begin's in-flight
	// wait has retired their frame). Both interleave FIFO in one ring.
	//
	// Sized for steady-state traffic, not load spikes: a request that does not
	// fit returns an invalid span and the caller falls back to a dedicated
	// one-shot staging buffer (initial scene load takes that path).
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

		// Every acquired span must be closed exactly once, with the transfer
		// timeline value that retires it: a slow job's span must ride ITS OWN
		// submission's value, not whatever submission happened to go out first.
		void Close(uint64_t spanId, uint64_t transferValue);

		// Free the closed-and-completed PREFIX.
		void Reclaim(uint64_t transferCompleted);

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
