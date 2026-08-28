#pragma once
#include <cassert>
#include "Graphics/ResourceHandle.h"

namespace Core
{
	/*
	 * Slot-based owning storage behind ResourceManager.
	 *
	 * Callers keep a Handle<T> and resolve it through Get(). A freed slot bumps its
	 * generation, so a handle kept past Remove() resolves to nullptr instead of
	 * silently aliasing whatever later took the slot.
	 *
	 * Slots hold a shared_ptr so the pool can co-own with legacy shared_ptr holders
	 * while a resource type is migrated onto handles. Once nothing else holds the
	 * resource, the pool is its sole owner and Remove() destroys it.
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

			// Reused slots may carry a stale state; resources are Resident by
			// default and only deferred-upload loaders mark Loading.
			slot.state = ResourceState::Resident;
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

		// Relocates the resource behind a live handle in place: the handle stays
		// valid (same index/generation) and now resolves to `resource`. This is the
		// primitive behind resize/relocation — a holder keeps its handle while the
		// backing object is swapped for a bigger one.
		//
		// The previous resource is released immediately, so only call this once no
		// in-flight frame still references it. Safe deferred destruction (retiring
		// the old object for MAX_FRAMES_IN_FLIGHT) is future work for streaming.
		void Replace(Handle<T> handle, shared_ptr<T> resource)
		{
			if (!IsAlive(handle))
				return;

			_slots[handle.index].resource = std::move(resource);
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

		// Residency is slot metadata beside the generation - never a member on
		// the resource object (transient load state must not outlive loading).
		bool IsResident(Handle<T> handle) const
		{
			return IsAlive(handle)
				&& _slots[handle.index].state == ResourceState::Resident;
		}

		void SetState(Handle<T> handle, ResourceState state)
		{
			if (IsAlive(handle))
				_slots[handle.index].state = state;
		}

		uint32_t GetLiveCount() const { return _liveCount; }
		uint32_t GetSlotCount() const { return static_cast<uint32_t>(_slots.size()); }

	private:
		struct Slot
		{
			shared_ptr<T> resource;
			uint32_t generation = 0;
			ResourceState state = ResourceState::Resident;
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

	template<typename T>
	bool Handle<T>::IsResident() const
	{
		return pool ? pool->IsResident(*this) : false;
	}

	template<typename T>
	void Handle<T>::SetLoading() const
	{
		if (pool)
			pool->SetState(*this, ResourceState::Loading);
	}

	template<typename T>
	void Handle<T>::SetResident() const
	{
		if (pool)
			pool->SetState(*this, ResourceState::Resident);
	}
}
