#pragma once
#include "Job.h"

namespace Core
{
	class CommandPool;
	class TransferThread
	{
	public:
		TransferThread(Device& device);
		~TransferThread();
		
		void UpdateFrame(uint32_t frame)
		{
			_currentFrame = frame;
		}

		void Enqueue(const Job* job);
		void Flush();
	private:
		void Run();
		void RecordAndSubmit();
	private:
		Device& _device;

		WorkQueue _workQueue;
		CommandPool* _commandPool;

		uint32_t _currentFrame = 0;
		vector<VkFence> _inFlightFences;
		vector<bool> _uploadCompletes;

		condition_variable _flushWait;
		condition_variable _fenceWait;

		bool _shutdown;
		bool _flushRequested;

		thread _workerThread;

		//todo : sub threads
		//todo : secondary command buffer
		//todo : TransferThread를 TransferMananger로 변경
		//todo : TransferManager는 메인 스레드에서 동작
		//todo : sub thread를 만들어서 secondary command buffer로 동작
		//todo : record and submit
	};
}

