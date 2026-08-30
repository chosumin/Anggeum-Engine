#include "stdafx.h"
#include "RetireQueue.h"
#include "SyncContext.h"

using namespace Core;

RetireQueue::RetireQueue(SyncContext& sync)
	: _sync(sync)
{
}

void RetireQueue::Retire(ErasedPtr resource)
{
	if (resource == nullptr)
		return;

	// The current values bound everything submitted so far.
	_entries.push_back({ std::move(resource),
		_sync.GetCurrentValue(QueueType::Graphics),
		_sync.GetCurrentValue(QueueType::Compute),
		_sync.GetCurrentValue(QueueType::Transfer) });
}

void RetireQueue::Collect()
{
	if (_entries.empty())
		return;

	u64 graphics = _sync.GetCompletedValue(QueueType::Graphics);
	u64 compute = _sync.GetCompletedValue(QueueType::Compute);
	u64 transfer = _sync.GetCompletedValue(QueueType::Transfer);

	while (!_entries.empty())
	{
		const Entry& front = _entries.front();
		if (graphics < front.graphics || compute < front.compute
			|| transfer < front.transfer)
			break;

		_entries.pop_front();
	}
}
