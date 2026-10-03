#pragma once
#include "WorkerThread.h"

namespace Core
{
	// Base for anything that farms jobs out to worker threads and waits on
	// their completion.
	class Threadable
	{
	public:
		Threadable(Device& device, SyncContext& syncContext, size_t threadCount,
			ThreadPriority priority = ThreadPriority::Normal);

	protected:
		// Takes ownership, queues the job on a worker thread, and keeps it alive
		// until ClearJobs.
		void Enqueue(unique_ptr<Job> job);

		// Wires the completion signal and queues the job WITHOUT taking
		// ownership: the caller keeps it alive until it reports COMPLETE.
		void EnqueueUnowned(Job& job);

		// Blocks until every tracked job reports COMPLETE.
		void WaitForJobs();

		// Blocks until `isComplete` holds - the seam for waiting on a SUBSET of
		// what was enqueued here.
		//
		// wait_for guards the missed-wakeup window: workers notify without
		// holding this mutex, so a notify can slip between the predicate check
		// and the sleep.
		template <typename Predicate>
		void WaitFor(Predicate isComplete)
		{
			unique_lock<mutex> lock(_waitLock);
			while (!isComplete())
				_completionWait.wait_for(lock, chrono::milliseconds(1));
		}

		// Enqueue order since the last ClearJobs.
		Job* GetJob(size_t index) const;
		size_t GetJobCount() const { return _pendingJobs.size(); }

		// Destroys the tracked jobs. Only call once their recorded work is no
		// longer referenced (submitted, or the frame slot was waited on).
		void ClearJobs();

	private:
		vector<unique_ptr<Job>> _pendingJobs;
		condition_variable _completionWait;
		mutex _waitLock;

		WorkerThreadManager _workers;
	};
}
