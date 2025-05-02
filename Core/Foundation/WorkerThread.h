#pragma once
#include "Job.h"
#include "Utils/timer.h"

namespace Core
{
	enum class QueueType
	{
		GRAPHICS, COMPUTE, TRANSFER
	};

	class CommandPool;
	class CommandBuffer;
	class WorkerThread
	{
	private:
		friend class TransferContext;
	public:
		WorkerThread(Device& device, condition_variable* fenceWait, 
			QueueType type, wstring threadName);
		~WorkerThread();

		void Enqueue(const Job* job);
		void Flush(VkCommandBufferLevel level);
		bool Complete();

		CommandBuffer& GetCommandBuffer(uint32_t currentFrame, VkCommandBufferLevel level);
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
		VkCommandBufferLevel _flushCommandLevel;
		condition_variable _flushWait;

		bool _shutdown;
		mutex _lock;
		thread _thread;
	};
}

