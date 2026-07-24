#pragma once
#include <cassert>
#include "Graphics/ResourceHandle.h"

namespace Core
{
	/*
	 * Slot-based owning storage behind ResourceCache.
	 *
	 * Callers keep a Handle<T> and resolve it through Get(). A freed slot bumps its
	 * generation, so a handle kept past Remove() resolves to nullptr instead of
	 * silently aliasing whatever later took the slot.
	 *
	 * Slots hold a shared_ptr so the pool can co-own with legacy shared_ptr holders
	 * while a resource type is migrated onto handles. Once nothing else holds the
	 * resource, the pool is its sole owner and Remove() destroys it.
	 *
	 * Not thread-safe on its own: ResourceCache serialises access with the mutex
	 * it already keeps per resource type.
	 */
	template<typename T>
	class ResourcePool
	{
	public:
		Handle<T> Add(shared_ptr<T> resource)
		{
			uint32_t index;

			if (!_freeSlots.empty())
			{
				index = _freeSlots.back();
				_freeSlots.pop_back();
			}
			else
			{
				index = static_cast<uint32_t>(_slots.size());
				_slots.emplace_back();
			}

			auto& slot = _slots[index];
			slot.resource = std::move(resource);
			++_liveCount;

			return Handle<T>{ this, index, slot.generation };
		}

		// Destroys the resource and invalidates every handle to it.
		void Remove(Handle<T> handle)
		{
			if (Get(handle) == nullptr)
				return;

			auto& slot = _slots[handle.index];
			slot.resource.reset();
			++slot.generation;
			--_liveCount;

			_freeSlots.push_back(handle.index);
		}

		// nullptr when the handle is default-constructed, out of range, or stale.
		T* Get(Handle<T> handle) const
		{
			if (!handle.IsValid() || handle.index >= _slots.size())
				return nullptr;

			const auto& slot = _slots[handle.index];
			if (slot.generation != handle.generation)
				return nullptr;

			return slot.resource.get();
		}

		bool IsAlive(Handle<T> handle) const { return Get(handle) != nullptr; }

		uint32_t GetLiveCount() const { return _liveCount; }
		uint32_t GetSlotCount() const { return static_cast<uint32_t>(_slots.size()); }

	private:
		struct Slot
		{
			shared_ptr<T> resource;
			uint32_t generation = 0;
		};

		vector<Slot> _slots;
		vector<uint32_t> _freeSlots;
		uint32_t _liveCount = 0;
	};

	// Defined here, where ResourcePool is complete.
	template<typename T>
	T* Handle<T>::TryGet() const
	{
		return pool ? pool->Get(*this) : nullptr;
	}

	template<typename T>
	T& Handle<T>::Get() const
	{
		T* resource = TryGet();
		assert(resource != nullptr && "Get() on an invalid or stale handle");
		return *resource;
	}
}
