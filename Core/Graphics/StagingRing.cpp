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
		return {};

	_head = start + aligned;
	_used += skip + aligned;
	_pendingUsed += skip + aligned;

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

	_stamps.push_back({ timelineValue, _pendingUsed });
	_pendingUsed = 0;
}

void StagingRing::Reclaim(uint64_t completedValue)
{
	lock_guard<mutex> lock(_mutex);

	// Stamps are monotonic, so completed ranges always form a prefix.
	while (!_stamps.empty() && _stamps.front().value <= completedValue)
	{
		_used -= _stamps.front().used;
		_stamps.pop_front();
	}
}
