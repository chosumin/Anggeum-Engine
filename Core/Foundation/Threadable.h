#pragma once
#include "Job.h"

namespace Core
{
	class WorkerThreadManager;

	// Base for anything that farms jobs out to the worker pool and waits on
	// their completion. Tracking the jobs here is optional: Enqueue takes
	// ownership, EnqueueUnowned only wires the job up, for owners that keep
	// richer per-job state in containers of their own.
	class Threadable
	{
	public:
		explicit Threadable(WorkerThreadManager& workerThreadManager);

	protected:
		WorkerThreadManager& GetWorkerThreadManager() { return _workerThreadManager; }

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
		WorkerThreadManager& _workerThreadManager;

		vector<unique_ptr<Job>> _pendingJobs;
		condition_variable _completionWait;
		mutex _waitLock;
	};
}
