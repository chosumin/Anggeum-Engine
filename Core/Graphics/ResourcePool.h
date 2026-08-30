#pragma once
#include <cassert>
#include "Graphics/ResourceHandle.h"
#include "Graphics/RetireQueue.h"

namespace Core
{
	/*
	 * Slot-based owning storage behind ResourceManager.
	 *
	 * Callers keep a Handle<T> and resolve it through Get(). A freed slot bumps its
	 * generation, so a handle kept past Remove() resolves to nullptr instead of
	 * silently aliasing whatever later took the slot.
	 *
	 * The pool tracks only the LOGICAL lifetime (generations). It never destroys:
	 * every resource displaced by Remove() or Replace() goes straight to the
	 * RetireQueue, which holds it until the GPU has passed its last use.
	 */
	template<typename T>
	class ResourcePool
	{
	public:
		explicit ResourcePool(RetireQueue& retire)
			: _retire(retire)
		{
		}

		Handle<T> Add(unique_ptr<T> resource)
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

		// Ends the LOGICAL lifetime: invalidates every handle (generation bump)
		// and frees the slot. The resource retires - it dies once no in-flight
		// submission can still reference it.
		void Remove(Handle<T> handle)
		{
			if (Get(handle) == nullptr)
				return;

			auto& slot = _slots[handle.index];
			_retire.Retire(std::move(slot.resource));
			++slot.generation;
			--_liveCount;

			_freeSlots.push_back(handle.index);
		}

		// Relocates the resource behind a live handle in place: the handle stays
		// valid (same index/generation) and now resolves to `resource`. This is the
		// primitive behind resize/relocation — a holder keeps its handle while the
		// backing object is swapped for a bigger one. The displaced resource
		// retires.
		void Replace(Handle<T> handle, unique_ptr<T> resource)
		{
			if (!IsAlive(handle))
				return;

			_retire.Retire(std::move(_slots[handle.index].resource));
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
			unique_ptr<T> resource;
			uint32_t generation = 0;
			ResourceState state = ResourceState::Resident;
		};

		RetireQueue& _retire;
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
