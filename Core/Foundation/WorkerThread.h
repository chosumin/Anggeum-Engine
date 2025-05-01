#pragma once
#include "Job.h"

namespace Core
{
	class CommandPool;

	class TransferContext
	{
	private:
		class WorkerThread
		{
		private:
			friend class TransferContext;
		public:
			WorkerThread(Device& device, condition_variable* fenceWait, size_t index);
			~WorkerThread();

			void Enqueue(const Job* job);
			void Flush();
		private:
			void Run();
			void Record();
		private:
			Device& _device;
			condition_variable* _fenceWait;

			uint32_t _currentFrame;
			CommandPool* _commandPool;
			vector<bool> _uploadCompletes;

			WorkQueue _workQueue;

			bool _shutdown;
			bool _flushRequested;

			condition_variable _flushWait;

			mutex _lock;
			thread _thread;

			//todo : secondary command buffer∑Œ µø¿€
		};
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

		uint32_t _currentFrame;
		vector<VkFence> _inFlightFences;

		condition_variable _fenceWait;
		mutex _lock;

		vector<const Job*> _reservedJobs;
	};
}

