#include "stdafx.h"
#include "Threadable.h"

Core::Threadable::Threadable(Device& device, SyncContext& syncContext, size_t threadCount,
	ThreadPriority priority)
	: _workers(device, syncContext, threadCount, priority)
{
}

void Core::Threadable::Enqueue(unique_ptr<Job> job)
{
	Job* raw = job.get();
	_pendingJobs.push_back(std::move(job));
	EnqueueUnowned(*raw);
}

void Core::Threadable::EnqueueUnowned(Job& job)
{
	job.completionWait = &_completionWait;
	_workers.Enqueue(&job);
}

void Core::Threadable::WaitForJobs()
{
	if (_pendingJobs.empty())
		return;

	WaitFor([this] {
		for (auto& job : _pendingJobs)
			if (job->status != JobStatus::COMPLETE)
				return false;
		return true;
	});
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
