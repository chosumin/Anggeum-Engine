#pragma once

namespace Core
{
	class SyncContext;

	// Deferred work on GPU ground truth: a retired object is held, and a
	// retired action is postponed, until every queue timeline has passed the
	// values that were current at retirement - no submission recorded before
	// the retire can still be running - then the object is dropped or the
	// action runs.
	//
	// NOT thread-safe on purpose: it runs on the main thread - loaders are the
	// only worker-thread pool users and they never retire.
	class RetireQueue
	{
	public:
		RetireQueue(SyncContext& sync);

		// Pending actions are dropped, not run: their targets may be gone.
		~RetireQueue() = default;

		template<typename T>
		void Retire(unique_ptr<T> resource)
		{
			Retire(ErasedPtr(resource.release(),
				[](void* pointer) { delete static_cast<T*>(pointer); }));
		}

		// Releasing a slot that pending submissions may still index goes here, 
		// so the slot and the object it belonged to leave together.
		void Retire(function<void()> onRetired);

		void Collect();

	private:
		using ErasedPtr = unique_ptr<void, void(*)(void*)>;

		void Retire(ErasedPtr resource);
		void Push(ErasedPtr resource, function<void()> action);

		struct Entry
		{
			ErasedPtr resource;
			function<void()> action;
			u64 graphics;
			u64 compute;
			u64 transfer;
		};

		SyncContext& _sync;
		deque<Entry> _entries;
	};
}
