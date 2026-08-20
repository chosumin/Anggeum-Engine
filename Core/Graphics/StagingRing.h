#pragma once

namespace Core
{
	class Device;
	class Buffer;

	// Persistently-mapped staging arena for upload jobs. Spans are handed out
	// circularly and reclaimed when the upload timeline passes the value they
	// were stamped with, so steady-state uploads reuse the same memory instead
	// of allocating a fresh staging buffer per job.
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

		// Everything acquired since the previous Stamp lives until the upload
		// timeline reaches `timelineValue` (stamped at submit)...
		void Stamp(uint64_t timelineValue);
		// ...and Reclaim frees every range whose stamped value has completed.
		void Reclaim(uint64_t completedValue);

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
		VkDeviceSize _pendingUsed = 0; // acquired since the last Stamp

		struct StampedRange
		{
			uint64_t value;
			VkDeviceSize used;
		};
		deque<StampedRange> _stamps;

		mutex _mutex;
	};
}
