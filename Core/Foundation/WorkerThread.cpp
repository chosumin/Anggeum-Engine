#include "stdafx.h"
#include "WorkerThread.h"
#include "Graphics/Vulkans/CommandPool.h"
#include "Graphics/Vulkans/CommandBuffer.h"

Core::TransferContext::WorkerThread::WorkerThread(Device& device, condition_variable* fenceWait, size_t index)
	:_device(device), _fenceWait(fenceWait), _currentFrame(0), 
	_shutdown(false), _flushRequested(false)
{
	QueueFamilyIndices indices = _device.GetQueueFamilyIndices();
	_commandPool = new CommandPool(device, indices.TransferFamily.value());

	_uploadCompletes.resize(MAX_FRAMES_IN_FLIGHT, false);

	_thread = thread(&WorkerThread::Run, this);
    SetThreadDescription(_thread.native_handle(), 
		(L"Transfer Thread " + to_wstring(index)).c_str());
}

Core::TransferContext::WorkerThread::~WorkerThread()
{
	{
		lock_guard<mutex> lock(_lock);
		_shutdown = true;
		_flushWait.notify_all();
	}

	_thread.join();

	delete(_commandPool);
}

void Core::TransferContext::WorkerThread::Enqueue(const Job* job)
{
	lock_guard<mutex> lock(_lock);
	_workQueue.Add(job);
}

void Core::TransferContext::WorkerThread::Flush()
{
	lock_guard<mutex> lock(_lock);

	if (_workQueue.Done())
		return;

	_flushRequested = true;
	_uploadCompletes[_currentFrame] = false;
	_flushWait.notify_one();
}

void Core::TransferContext::WorkerThread::Run()
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

void Core::TransferContext::WorkerThread::Record()
{
	_commandPool->ResetCommandBuffers(_currentFrame);

	auto& commandBuffer = _commandPool->RequestCommandBuffer(_currentFrame);
	commandBuffer.BeginCommandBuffer();

	while (_workQueue.Done() == false) 
	{
		Job* pendingJob = _workQueue.GetNext();

		if (!pendingJob)
			break;

		pendingJob->status = JobStatus::PROGRESS;
		pendingJob->Execute(commandBuffer);
	}

	commandBuffer.EndCommandBuffer();
	
	//todo : wait imageAvailable semaphore
	//todo : signal renderAvailable semaphore
}

Core::TransferContext::TransferContext(Device& device)
	:_device(device)
{
	_threadsReadyCount = 0;
	
	_threadCount = 3;
	for (size_t i = 0; i < _threadCount; i++)
	{
		auto workerThread = make_unique<WorkerThread>(_device, &_fenceWait, i);
		_workerThreads.push_back(move(workerThread));
	}

	_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		if (vkCreateFence(_device.GetDevice(), &fenceInfo, nullptr, &_inFlightFences[i]) != VK_SUCCESS)
			throw runtime_error("failed to create semaphores!");
	}
}

Core::TransferContext::~TransferContext()
{
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		vkDestroyFence(_device.GetDevice(), _inFlightFences[i], nullptr);
	}
}

void Core::TransferContext::UpdateFrame(uint32_t frame)
{
	_currentFrame = frame;

	for (auto& thread : _workerThreads)
	{
		thread->_currentFrame = frame;
	}
}

void Core::TransferContext::Enqueue(const Job* job)
{
	_reservedJobs.push_back(job);
}

void Core::TransferContext::Flush()
{
	if (_reservedJobs.size() <= 0)
		return;

	size_t jobCount = _reservedJobs.size();
	uint32_t threadIndex = 0;
	while (_reservedJobs.empty() == false)
	{
		auto job = _reservedJobs.back();
		_reservedJobs.pop_back();
		_workerThreads[threadIndex]->Enqueue(job);
		threadIndex = (threadIndex + 1) % _threadCount;
	}

	_threadsReadyCount = std::min(jobCount, _threadCount);

	for (size_t i = 0; i < _threadsReadyCount; i++)
	{
		_workerThreads[i]->Flush();
	}

	unique_lock<mutex> lock(_lock);
	_fenceWait.wait(lock, [&]
	{ 
		bool complete = true;
		for (size_t i = 0; i < _threadsReadyCount; i++)
		{
			if (_workerThreads[i]->_uploadCompletes[_currentFrame] == false)
			{
				complete = false;
				return false;
			}
		}
		return complete;
	});

	vkResetFences(_device.GetDevice(), 1, &_inFlightFences[_currentFrame]);

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = static_cast<uint32_t>(_threadsReadyCount);

	vector<VkCommandBuffer> commandBuffers(_threadsReadyCount);
	for (size_t i = 0; i < _threadsReadyCount; i++)
	{
		commandBuffers[i] = _workerThreads[i]->_commandPool->
			GetCommandBuffer(_currentFrame).GetHandle();
	}
	submitInfo.pCommandBuffers = commandBuffers.data();

	VkQueue queue = _device.GetTransferQueue();
	vkQueueSubmit(queue, 1, &submitInfo, _inFlightFences[_currentFrame]);

	lock.unlock();
	_threadsReadyCount = 0;

	vkWaitForFences(_device.GetDevice(), 1, &_inFlightFences[_currentFrame], VK_TRUE, 100000000000);
}
