#include "stdafx.h"
#include "TerrainSlotAllocator.h"
#include "Graphics/SyncContext.h"

namespace Core
{
	TerrainSlotAllocator::TerrainSlotAllocator(SyncContext& syncContext, uint32_t capacity)
		: _sync(syncContext), _capacity(capacity)
	{
		_freeSlots.reserve(capacity);
		for (uint32_t slot = capacity; slot > 0; --slot)
			_freeSlots.push_back(uint16_t(slot - 1));
	}

	uint16_t TerrainSlotAllocator::Allocate()
	{
		if (_freeSlots.empty())
		{
			// Reclaim retired slots once the GPU passed their last possible
			// reader. The per-frame cache is enough: stale merely delays.
			const u64 completed = _sync.GetCompletedValue(QueueType::Graphics);
			while (!_retiredSlots.empty() && completed >= _retiredSlots.front().second)
			{
				_freeSlots.push_back(_retiredSlots.front().first);
				_retiredSlots.pop_front();
			}
		}

		if (_freeSlots.empty())
			return TERRAIN_NODE_EMPTY;

		uint16_t slot = _freeSlots.back();
		_freeSlots.pop_back();
		return slot;
	}

	void TerrainSlotAllocator::Release(uint16_t slot)
	{
		assert(slot < _capacity);
		_retiredSlots.emplace_back(slot, _sync.GetCurrentValue(QueueType::Graphics));
	}
}
