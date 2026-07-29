#pragma once
#include "Foundation/Job.h"
#include "Utils/timer.h"

namespace Core
{
	class CommandPool;
	class WorkerThreadManager;
	class TransferContext
	{
	public:
		TransferContext(Device& device, WorkerThreadManager& workerThreadManager);
		~TransferContext();

		void UpdateFrame(uint32_t frame);

		// Takes ownership of the job. A name already pending drops the new job
		// (destroyed on return) — the queued one covers the request.
		void Enqueue(unique_ptr<Job> job, const string& jobName);
		void Wait();
	private:
		void ClearJobs();
	private:
		Device& _device;

		WorkerThreadManager& _workerThreadManager;

		CommandPool* _primaryCommandPool;
		uint32_t _currentFrame;
		vector<VkFence> _inFlightFences;

		condition_variable _fenceWait;
		mutex _lock;

		Core::Timer _timer;

		unordered_map<string, unique_ptr<Job>> _pendingJobs;
	};
}

