#pragma once
#include "Job.h"

namespace Core
{
	class WorkerThreadManager;
	class Threadable
	{
	public:
		explicit Threadable(WorkerThreadManager& workerThreadManager);

	protected:
		WorkerThreadManager& GetWorkerThreadManager() { return _workerThreadManager; }

		// Takes ownership, queues the job on a worker thread, and keeps it alive
		// until ClearJobs.
		void Enqueue(unique_ptr<Job> job);

		// Blocks until every tracked job reports COMPLETE.
		void WaitForJobs();

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
