#include "stdafx.h"
#include "StagingRing.h"
#include "Graphics/Vulkans/Buffer.h"

using namespace Core;

StagingRing::StagingRing(Device& device, VkDeviceSize capacity)
	: _capacity(capacity)
{
	_buffer = make_unique<Buffer>(device, capacity,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryType::STAGE);

	void* mapped = nullptr;
	_buffer->GetMappedPtr(&mapped);
	_mapped = static_cast<uint8_t*>(mapped);
}

StagingRing::~StagingRing() = default;

StagingRing::Span StagingRing::Acquire(VkDeviceSize size)
{
	VkDeviceSize aligned = ((size + ALIGNMENT - 1) / ALIGNMENT) * ALIGNMENT;

	lock_guard<mutex> lock(_mutex);

	_frameRequested += size;

	// A span never wraps: when it does not fit before the ring's end, the
	// remainder is skipped (and counted as used, so it reclaims with this
	// stamp range like any other bytes).
	VkDeviceSize start = _head;
	VkDeviceSize skip = 0;
	if (start + aligned > _capacity)
	{
		skip = _capacity - start;
		start = 0;
	}

	if (_used + skip + aligned > _capacity)
	{
		++_fallbacks;
		return {};
	}

	_head = start + aligned;
	_used += skip + aligned;
	_pendingUsed += skip + aligned;
	_peakUsed = std::max(_peakUsed, _used);

	Span span;
	span.buffer = _buffer.get();
	span.offset = start;
	span.mapped = _mapped + start;
	return span;
}

void StagingRing::Stamp(uint64_t timelineValue)
{
	lock_guard<mutex> lock(_mutex);

	if (_pendingUsed == 0)
		return;

	_stamps.push_back({ false, timelineValue, _pendingUsed });
	_pendingUsed = 0;
}

void StagingRing::StampForFrameSlot(uint64_t firstSafeFrame)
{
	lock_guard<mutex> lock(_mutex);

	if (_pendingUsed == 0)
		return;

	_stamps.push_back({ true, firstSafeFrame, _pendingUsed });
	_pendingUsed = 0;
}

void StagingRing::Reclaim(uint64_t transferCompleted, uint64_t currentFrame)
{
	lock_guard<mutex> lock(_mutex);

	// Stamps are FIFO and reclamation is positional, so only a completed
	// PREFIX can free. A still-pending range blocks the ones behind it - fine,
	// both timelines retire within a couple of frames.
	while (!_stamps.empty())
	{
		const StampedRange& front = _stamps.front();
		bool completed = front.frameSlot
			? currentFrame >= front.value
			: transferCompleted >= front.value;
		if (!completed)
			break;

		_used -= front.used;
		_stamps.pop_front();
	}
}

void StagingRing::BeginFrame()
{
	lock_guard<mutex> lock(_mutex);
	_frameRequested = 0;
}

VkDeviceSize StagingRing::GetUsed()
{
	lock_guard<mutex> lock(_mutex);
	return _used;
}

VkDeviceSize StagingRing::GetPeakUsed()
{
	lock_guard<mutex> lock(_mutex);
	return _peakUsed;
}

VkDeviceSize StagingRing::GetFrameRequested()
{
	lock_guard<mutex> lock(_mutex);
	return _frameRequested;
}

uint32_t StagingRing::GetFallbackCount()
{
	lock_guard<mutex> lock(_mutex);
	return _fallbacks;
}
