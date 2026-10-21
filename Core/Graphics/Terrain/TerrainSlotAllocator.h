#pragma once
#include "TerrainConfig.h"

namespace Core
{
	class SyncContext;

	// A fixed pool of texture slots (atlas tiles, cache pages): a free list
	// plus a frame-stamped retire queue, so a released slot re-enters the
	// free list only once the GPU passed its last possible reader.
	class TerrainSlotAllocator
	{
	public:
		TerrainSlotAllocator(SyncContext& syncContext, uint32_t capacity);

		// TERRAIN_NODE_EMPTY when no slot can be handed out safely this frame
		// (all occupied or still retiring).
		uint16_t Allocate();

		// The caller's tables must stop listing the slot this same frame: the
		// stamp bounds every frame submitted so far, not the one being built.
		void Release(uint16_t slot);

		uint32_t GetCapacity() const { return _capacity; }
		uint32_t GetFreeCount() const
		{
			return uint32_t(_freeSlots.size() + _retiredSlots.size());
		}

	private:
		SyncContext& _sync;
		uint32_t _capacity;

		vector<uint16_t> _freeSlots;

		// slot + the graphics value covering its last possible reader.
		deque<pair<uint16_t, u64>> _retiredSlots;
	};
}
