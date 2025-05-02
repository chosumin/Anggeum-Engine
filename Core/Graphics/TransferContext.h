#pragma once
#include "Foundation/Job.h"
#include "Utils/timer.h"

namespace Core
{
	class CommandPool;
	class WorkerThread;
	class TransferContext
	{
	public:
		TransferContext(Device& device);
		~TransferContext();

		void UpdateFrame(uint32_t frame);

		void Enqueue(const Job* job);
		void Flush();
	private:
		Device& _device;

		size_t _threadCount;
		vector<unique_ptr<WorkerThread>> _workerThreads;

		size_t _threadsReadyCount;
		size_t _ringIndex = 0;

		CommandPool* _primaryCommandPool;
		uint32_t _currentFrame;
		vector<VkFence> _inFlightFences;

		condition_variable _fenceWait;
		mutex _lock;

		Core::Timer _timer;
	};
}

