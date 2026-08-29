#pragma once
#include "Job.h"
#include "Utils/timer.h"

namespace Core
{
	class SyncContext;
	class CommandPool;
	class CommandBuffer;
	class WorkerThread
	{
	private:
		friend class WorkerThreadManager;
	public:
		WorkerThread(Device& device, SyncContext& syncContext);
		~WorkerThread();

		void Enqueue(const Job* job);
		bool Complete();
	private:
		void Run();
		CommandBuffer* RequestAndBeginCommandBuffer(Job* job);
	private:
		Device& _device;
		WorkQueue _workQueue;

		CommandPool* _graphicsCommandPool;
		CommandPool* _computeCommandPool;
		CommandPool* _transferCommandPool;

		condition_variable _flushWait;

		bool _shutdown;
		thread _thread;
		mutex _lock;
	};

	class WorkerThreadManager
	{
	public:
		WorkerThreadManager(Device& device, SyncContext& syncContext);
		~WorkerThreadManager() = default;

		void Enqueue(const Job* job);
	private:
		Device& _device;

		size_t _threadCount;
		vector<unique_ptr<WorkerThread>> _workerThreads;
		atomic<size_t> _roundRobinIndex;

		mutex _lock;
	};
}

