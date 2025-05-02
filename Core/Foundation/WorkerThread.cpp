#include "stdafx.h"
#include "WorkerThread.h"
#include "Graphics/Vulkans/CommandPool.h"
#include "Graphics/Vulkans/CommandBuffer.h"

Core::WorkerThread::WorkerThread(Device& device, condition_variable* fenceWait, 
	QueueType type, wstring threadName)
	:_device(device), _fenceWait(fenceWait), _currentFrame(0), 
	_shutdown(false), _flushRequested(false)
{
	QueueFamilyIndices indices = _device.GetQueueFamilyIndices();

	switch (type)
	{
	case QueueType::GRAPHICS:
		_commandPool = new CommandPool(device, indices.GraphicsAndComputeFamily.value());
		break;
	case QueueType::COMPUTE:
		_commandPool = new CommandPool(device, indices.GraphicsAndComputeFamily.value());
		break;
	case QueueType::TRANSFER:
		_commandPool = new CommandPool(device, indices.TransferFamily.value());
		break;
	}

	_uploadCompletes.resize(MAX_FRAMES_IN_FLIGHT, false);

	_thread = thread(&WorkerThread::Run, this);
    SetThreadDescription(_thread.native_handle(), 
		threadName.c_str());
}

Core::WorkerThread::~WorkerThread()
{
	{
		lock_guard<mutex> lock(_lock);
		_shutdown = true;
		_flushWait.notify_all();
	}

	_thread.join();

	delete(_commandPool);
}

void Core::WorkerThread::Enqueue(const Job* job)
{
	lock_guard<mutex> lock(_lock);
	_workQueue.Add(job);
}

void Core::WorkerThread::Flush(VkCommandBufferLevel level)
{
	lock_guard<mutex> lock(_lock);

	if (_workQueue.Done())
		return;

	_flushRequested = true;
	_flushCommandLevel = level;
	_uploadCompletes[_currentFrame] = false;
	_flushWait.notify_one();
}

bool Core::WorkerThread::Complete()
{
	return _uploadCompletes[_currentFrame];
}

Core::CommandBuffer& Core::WorkerThread::GetCommandBuffer(uint32_t currentFrame, VkCommandBufferLevel level)
{
	return _commandPool->GetCommandBuffer(currentFrame, level);
}

void Core::WorkerThread::Run()
{
	while (true)
	{
		unique_lock<mutex> lock(_lock);

		_flushWait.wait(lock, [&] {return _flushRequested || _shutdown; });

		if (_shutdown)
			break;

		if (_flushRequested && !_workQueue.Done())
		{
			_flushRequested = false;

			lock.unlock();
			
			Record();

			{
				lock_guard<mutex> lock(_lock);
				_uploadCompletes[_currentFrame] = true;
			}
			_fenceWait->notify_one();
		}
	}
}

void Core::WorkerThread::Record()
{
	auto& commandBuffer = 
		_commandPool->RequestCommandBuffer(_currentFrame, _flushCommandLevel);

	commandBuffer.BeginCommandBuffer(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		nullptr, nullptr, 0, _currentFrame);

	while (_workQueue.Done() == false) 
	{
		Job* pendingJob = _workQueue.GetNext();

		if (!pendingJob)
			break;

		pendingJob->status = JobStatus::PROGRESS;
		pendingJob->Execute(commandBuffer);
	}

	commandBuffer.EndCommandBuffer();
}