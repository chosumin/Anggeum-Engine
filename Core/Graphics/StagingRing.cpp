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
	// remainder is skipped (and counted as this span's bytes, so it reclaims
	// with it).
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
	_peakUsed = std::max(_peakUsed, _used);

	Span span;
	span.buffer = _buffer.get();
	span.offset = start;
	span.mapped = _mapped + start;
	span.id = _baseId + _records.size();
	_records.push_back({ skip + aligned });
	return span;
}

void StagingRing::Close(uint64_t spanId, uint64_t transferValue)
{
	lock_guard<mutex> lock(_mutex);

	assert(spanId >= _baseId && spanId - _baseId < _records.size()
		&& "closing an unknown or already reclaimed span");

	SpanRecord& record = _records[size_t(spanId - _baseId)];
	record.closed = true;
	record.value = transferValue;
}

void StagingRing::Reclaim(uint64_t transferCompleted)
{
	lock_guard<mutex> lock(_mutex);

	// An unclosed span (its job is still recording) blocks the ranges behind it -
	// conservative but safe, and it resolves as soon as that job submits.
	while (!_records.empty())
	{
		const SpanRecord& front = _records.front();
		if (!front.closed || transferCompleted < front.value)
			break;

		_used -= front.used;
		_records.pop_front();
		++_baseId;
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
