#pragma once
#include "SyncContext.h"

namespace Core
{
	class Device;
	class CommandBuffer;

	// One pass's GPU execution interval, relative to the frame's first timestamp.
	struct PassTiming
	{
		const char* name = "";
		QueueType   queue = QueueType::Graphics;
		double      beginMs = 0.0;
		double      endMs = 0.0;

		double DurationMs() const { return endMs - beginMs; }
	};

	struct QueueTimings
	{
		bool valid = false;

		// Time each queue actually had work executing. Stalls are excluded: this is
		// the sum of the per-pass intervals, not the span from first to last pass.
		double graphicsBusyMs = 0.0;
		double computeBusyMs = 0.0;

		// Wall-clock length of the whole GPU frame: earliest timestamp to latest.
		// The gap between this and a queue's busy time is that queue idling mid
		// frame — waiting on the other queue, or starved by the CPU.
		double frameSpanMs = 0.0;

		// Time both queues had work executing simultaneously — the real async
		// compute win.
		double overlapMs = 0.0;

		// Overlap as a share of the queue that ran less. 0% means the queues never
		// ran together and async compute is buying nothing.
		double overlapPercent = 0.0;

		vector<PassTiming> passes;
	};

	// Brackets every pass with GPU timestamps so the graphics and compute queues'
	// real execution intervals can be compared.
	//
	// Frame captures serialize submissions and so cannot show whether the queues run
	// concurrently. These timestamps are written by the GPU itself, so they can.
	class GpuQueueTimer
	{
	public:
		explicit GpuQueueTimer(Device& device);
		~GpuQueueTimer();

		// False when the device or one of the queue families cannot write
		// timestamps. Every call below then does nothing and Resolve() reports an
		// invalid result.
		bool IsSupported() const { return _supported; }

		// Recorded at the very start / very end of each pass's command buffer.
		// `name` must outlive the timer (a string literal or typeid name).
		void BeginPass(CommandBuffer& commandBuffer, uint32_t frameIndex,
			uint32_t passIndex, QueueType queue, const char* name);
		void EndPass(CommandBuffer& commandBuffer, uint32_t frameIndex, uint32_t passIndex);

		// Only meaningful once this slot's GPU work is known to have completed.
		QueueTimings Resolve(uint32_t frameIndex);

	private:
		static constexpr uint32_t MaxPasses = 32;
		static constexpr uint32_t QueriesPerPass = 2;
		static constexpr uint32_t QueriesPerFrame = MaxPasses * QueriesPerPass;

		uint32_t BeginQuery(uint32_t frameIndex, uint32_t passIndex) const
		{
			return frameIndex * QueriesPerFrame + passIndex * QueriesPerPass;
		}

	private:
		struct FrameRecord
		{
			uint32_t passCount = 0;
			array<QueueType, MaxPasses> queues{};
			array<const char*, MaxPasses> names{};
		};

		Device& _device;

		VkQueryPool _queryPool = VK_NULL_HANDLE;
		bool _supported = false;

		// Nanoseconds per timestamp tick.
		float _timestampPeriod = 1.0f;

		// A queue may report fewer than 64 meaningful timestamp bits; the rest are
		// undefined and must be masked off before the values are compared.
		uint64_t _graphicsMask = ~0ull;
		uint64_t _computeMask = ~0ull;

		array<FrameRecord, MAX_FRAMES_IN_FLIGHT> _records;
	};
}
