#include "stdafx.h"
#include "WorkerThread.h"
#include "Graphics/Vulkans/CommandPool.h"
#include "Graphics/SyncContext.h"
#include "Graphics/Vulkans/CommandBuffer.h"

Core::WorkerThread::WorkerThread(Device& device, SyncContext& syncContext)
	:_device(device), _shutdown(false)
{

	_graphicsCommandPool = new CommandPool(device, syncContext, QueueType::Graphics);
	_computeCommandPool = new CommandPool(device, syncContext, QueueType::Compute);
	_transferCommandPool = new CommandPool(device, syncContext, QueueType::Transfer);

	_thread = thread(&WorkerThread::Run, this);
    SetThreadDescription(_thread.native_handle(), L"Worker Thread");
}

Core::WorkerThread::~WorkerThread()
{
	{
		lock_guard<mutex> lock(_lock);
		_shutdown = true;
		_flushWait.notify_all();
	}

	_thread.join();

	delete(_graphicsCommandPool);
	delete(_computeCommandPool);
	delete(_transferCommandPool);
}

void Core::WorkerThread::Enqueue(const Job* job)
{
	lock_guard<mutex> lock(_lock);

	_workQueue.Add(job);
	_flushWait.notify_one();
}

bool Core::WorkerThread::Complete()
{
	return _workQueue.Done();
}

void Core::WorkerThread::Run()
{
	while (_shutdown == false)
	{
		unique_lock<mutex> lock(_lock);

		_flushWait.wait(lock, [&] {return !_workQueue.Done() || _shutdown; });

		if (_shutdown)
			break;

		if (!_workQueue.Done())
		{
			Job* pendingJob = _workQueue.Pop();
			if (!pendingJob)
				continue;

			lock.unlock();

			auto commandBuffer = RequestAndBeginCommandBuffer(pendingJob);

			pendingJob->status = JobStatus::PROGRESS;
			pendingJob->Execute();

			commandBuffer->EndCommandBuffer();

			pendingJob->status = JobStatus::COMPLETE;

			pendingJob->completionWait->notify_one();
		}
	}
}

Core::CommandBuffer* Core::WorkerThread::RequestAndBeginCommandBuffer(Job* job)
{
	switch (job->type)
	{
	case JobType::GRAPHICS_PRIMARY:
	{
		auto& commandBuffer = _graphicsCommandPool->RequestCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
		commandBuffer.BeginCommandBuffer();
		job->commandBuffer = &commandBuffer;
		return &commandBuffer;
	}
	case JobType::GRAPHICS_SECONDARY:
	{
		auto& commandBuffer = _graphicsCommandPool->RequestCommandBuffer(VK_COMMAND_BUFFER_LEVEL_SECONDARY);
		commandBuffer.BeginCommandBuffer(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		job->commandBuffer = &commandBuffer;
		return &commandBuffer;
	}
	case JobType::COMPUTE:
	{
		auto& commandBuffer = _computeCommandPool->RequestCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
		commandBuffer.BeginCommandBuffer();
		job->commandBuffer = &commandBuffer;
		return &commandBuffer;
	}
	case JobType::TRANSFER:
	{
		auto& commandBuffer = _transferCommandPool->RequestCommandBuffer(VK_COMMAND_BUFFER_LEVEL_SECONDARY);
		commandBuffer.BeginCommandBuffer(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
		job->commandBuffer = &commandBuffer;
		return &commandBuffer;
	}
	}

	throw runtime_error("Invalid job type");
}

Core::WorkerThreadManager::WorkerThreadManager(Device& device, SyncContext& syncContext)
	:_device(device)
{
	_threadCount = std::thread::hardware_concurrency();

	for (size_t i = 0; i < _threadCount; i++)
	{
		auto workerThread = make_unique<WorkerThread>(_device, syncContext);
		_workerThreads.push_back(move(workerThread));
	}
}

void Core::WorkerThreadManager::Enqueue(const Job* job)
{
	_roundRobinIndex = _roundRobinIndex % _threadCount;
	WorkerThread& thread = *_workerThreads[_roundRobinIndex];
	_workerThreads[_roundRobinIndex]->Enqueue(job);
	++_roundRobinIndex;
}