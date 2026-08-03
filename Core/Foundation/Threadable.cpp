#include "stdafx.h"
#include "Threadable.h"
#include "WorkerThread.h"

Core::Threadable::Threadable(WorkerThreadManager& workerThreadManager)
	: _workerThreadManager(workerThreadManager)
{
}

void Core::Threadable::Enqueue(unique_ptr<Job> job)
{
	job->completionWait = &_completionWait;

	Job* raw = job.get();
	_pendingJobs.push_back(std::move(job));
	_workerThreadManager.Enqueue(raw);
}

void Core::Threadable::WaitForJobs()
{
	if (_pendingJobs.empty())
		return;

	unique_lock<mutex> lock(_waitLock);
	auto allComplete = [&] {
		for (auto& job : _pendingJobs)
			if (job->status != JobStatus::COMPLETE)
				return false;
		return true;
	};

	// wait_for guards the missed-wakeup window: workers notify without holding
	// this mutex, so a notify can slip between the predicate check and the sleep.
	while (!allComplete())
		_completionWait.wait_for(lock, std::chrono::milliseconds(1));
}

Core::Job* Core::Threadable::GetJob(size_t index) const
{
	assert(index < _pendingJobs.size());
	return _pendingJobs[index].get();
}

void Core::Threadable::ClearJobs()
{
	_pendingJobs.clear();
}
