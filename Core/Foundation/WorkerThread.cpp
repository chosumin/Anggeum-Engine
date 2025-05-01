#include "stdafx.h"
#include "WorkerThread.h"
#include "Graphics/Vulkans/CommandPool.h"
#include "Graphics/Vulkans/CommandBuffer.h"

Core::TransferThread::TransferThread(Device& device)
	:_device(device)
{
	QueueFamilyIndices indices = _device.GetQueueFamilyIndices();

	_commandPool = new CommandPool(device, indices.GraphicsAndComputeFamily.value());

	_workerThread = thread(&TransferThread::Run, this);

	_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		if (vkCreateFence(_device.GetDevice(), &fenceInfo, nullptr, &_inFlightFences[i]) != VK_SUCCESS)
			throw runtime_error("failed to create semaphores!");
	}

	_uploadCompletes.resize(MAX_FRAMES_IN_FLIGHT, false);
}

Core::TransferThread::~TransferThread()
{
	{
		lock_guard<mutex> lock(_workQueue.lock);
		_shutdown = true;
		_flushWait.notify_all();
	}

	_workerThread.join();

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		vkDestroyFence(_device.GetDevice(), _inFlightFences[i], nullptr);
	}

	delete(_commandPool);
}

void Core::TransferThread::Enqueue(const Job* job)
{
	lock_guard<mutex> lock(_workQueue.lock);
	_workQueue.Add(job);
}

void Core::TransferThread::Flush()
{
	{
		lock_guard<mutex> lock(_workQueue.lock);

		if (_workQueue.Done())
			return;

		_flushRequested = true;
		_flushWait.notify_one();
	}

	unique_lock<mutex> lock(_workQueue.lock);
	_fenceWait.wait(lock, [&] { return _uploadCompletes[_currentFrame]; });
}

void Core::TransferThread::Run()
{
	while (true)
	{
		unique_lock<mutex> lock(_workQueue.lock);

		_flushWait.wait(lock, [&] {return _flushRequested || _shutdown; });

		if (_shutdown)
			break;

		if (_flushRequested && !_workQueue.Done())
		{
			_flushRequested = false;

			lock.unlock();
			
			RecordAndSubmit();

			{
				lock_guard<mutex> lock(_workQueue.lock);
				_uploadCompletes[_currentFrame] = true;
			}
			_fenceWait.notify_one();
		}
	}
}

void Core::TransferThread::RecordAndSubmit()
{
	vkResetFences(_device.GetDevice(), 1, &_inFlightFences[_currentFrame]);

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

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer.GetHandle();
	
	//todo : wait imageAvailable semaphore
	//todo : signal renderAvailable semaphore
	
	VkQueue queue = _device.GetTransferQueue();
	vkQueueSubmit(queue, 1, &submitInfo, _inFlightFences[_currentFrame]);

	vkWaitForFences(_device.GetDevice(), 1, &_inFlightFences[_currentFrame], VK_TRUE, 100000000000);
}