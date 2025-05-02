#pragma once
#include "Job.h"
#include "Utils/timer.h"

namespace Core
{
	class CommandPool;
	class WorkerThread
	{
	private:
		friend class TransferContext;
	public:
		WorkerThread(Device& device, condition_variable* fenceWait, wstring threadName);
		~WorkerThread();

		void Enqueue(const Job* job);
		void Flush();
	private:
		void Run();
		void Record();
	private:
		Device& _device;
		condition_variable* _fenceWait;
		WorkQueue _workQueue;

		uint32_t _currentFrame;
		CommandPool* _commandPool;
		vector<bool> _uploadCompletes;

		bool _flushRequested;
		condition_variable _flushWait;

		bool _shutdown;
		mutex _lock;
		thread _thread;
	};
}

