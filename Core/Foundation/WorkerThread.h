#pragma once
#include "Job.h"
#include "Utils/timer.h"

namespace Core
{
	class SyncContext;
	class CommandPool;
	class CommandBuffer;

	enum class ThreadPriority
	{
		Normal,
		BelowNormal
	};

	class WorkerThread
	{
	private:
		friend class WorkerThreadManager;
	public:
		WorkerThread(Device& device, SyncContext& syncContext, ThreadPriority priority);
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

	// A plain pool: the owner decides what shares it. Keep long-running jobs
	// in a pool of their own, or short ones queue behind them.
	class WorkerThreadManager
	{
	public:
		WorkerThreadManager(Device& device, SyncContext& syncContext, size_t threadCount,
			ThreadPriority priority = ThreadPriority::Normal);
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

