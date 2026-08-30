#pragma once

namespace Core
{
	class SyncContext;

	// Deferred destruction on GPU ground truth: a retired object is held until
	// every queue timeline has passed the values that were current at
	// retirement - no submission recorded before the retire can still be
	// running - then dropped.
	//
	// NOT thread-safe on purpose: it runs on the main thread - loaders are the
	// only worker-thread pool users and they never retire.
	class RetireQueue
	{
	public:
		RetireQueue(SyncContext& sync);

		template<typename T>
		void Retire(unique_ptr<T> resource)
		{
			Retire(ErasedPtr(resource.release(),
				[](void* pointer) { delete static_cast<T*>(pointer); }));
		}

		void Collect();

	private:
		using ErasedPtr = unique_ptr<void, void(*)(void*)>;

		void Retire(ErasedPtr resource);

		struct Entry
		{
			ErasedPtr resource;
			u64 graphics;
			u64 compute;
			u64 transfer;
		};

		SyncContext& _sync;
		deque<Entry> _entries;
	};
}
