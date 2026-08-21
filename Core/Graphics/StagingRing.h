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

			bool IsValid() const { return buffer != nullptr; }
		};

		StagingRing(Device& device, VkDeviceSize capacity);
		~StagingRing();

		// Thread-safe: jobs acquire on worker threads while they record.
		Span Acquire(VkDeviceSize size);

		// Everything acquired since the previous stamp retires when the upload
		// timeline reaches `timelineValue` (stamped at submit)...
		void Stamp(uint64_t timelineValue);
		// ...or, for spans the frame graph consumes, when the frame counter
		// reaches `firstSafeFrame` (Begin's wait has retired their slot).
		void StampForFrameSlot(uint64_t firstSafeFrame);

		// Free every range whose stamped timeline has passed.
		void Reclaim(uint64_t transferCompleted, uint64_t currentFrame);

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
		VkDeviceSize _pendingUsed = 0; // acquired since the last stamp

		struct StampedRange
		{
			bool frameSlot;  // which timeline `value` belongs to
			uint64_t value;
			VkDeviceSize used;
		};
		deque<StampedRange> _stamps;

		VkDeviceSize _peakUsed = 0;
		VkDeviceSize _frameRequested = 0;
		uint32_t _fallbacks = 0;

		mutex _mutex;
	};
}
