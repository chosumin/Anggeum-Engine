#include "stdafx.h"
#include "TransferContext.h"
#include "Foundation/WorkerThread.h"
#include "Graphics/Vulkans/CommandPool.h"
#include "Graphics/Vulkans/CommandBuffer.h"

Core::TransferContext::TransferContext(Device& device)
	:_device(device)
{
	_threadsReadyCount = 0;
	
	_threadCount = 3;
	for (size_t i = 0; i < _threadCount; i++)
	{
		auto workerThread = make_unique<WorkerThread>(_device, &_fenceWait, (L"Transfer Thread " + to_wstring(i)));
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

	_primaryCommandPool = new CommandPool(_device,
		_device.GetQueueFamilyIndices().TransferFamily.value(),
		VK_COMMAND_BUFFER_LEVEL_PRIMARY);
}

Core::TransferContext::~TransferContext()
{
	delete(_primaryCommandPool);

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
	_workerThreads[_ringIndex]->Enqueue(job);
	_ringIndex = (_ringIndex + 1) % _threadCount;
	_threadsReadyCount = std::min(_threadsReadyCount + 1, _threadCount);
}

void Core::TransferContext::Flush()
{
	if (_threadsReadyCount <= 0)
		return;

	_timer.tick();

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

	auto& primary = _primaryCommandPool->RequestCommandBuffer(_currentFrame);
	primary.BeginCommandBuffer(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		nullptr, nullptr, 0, _currentFrame);

	vector<CommandBuffer*> secondaryCommands(_threadsReadyCount);
	for (size_t i = 0; i < _threadsReadyCount; i++)
	{
		secondaryCommands[i] = 
			&(_workerThreads[i]->_commandPool->GetCommandBuffer(_currentFrame));
	}

	primary.ExecuteCommands(secondaryCommands);

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &primary.GetHandle();

	primary.EndCommandBuffer();

	VkQueue queue = _device.GetTransferQueue();
	vkQueueSubmit(queue, 1, &submitInfo, _inFlightFences[_currentFrame]);

	lock.unlock();
	
	_threadsReadyCount = 0;
	_ringIndex = 0;


	//todo : wait imageAvailable semaphore
	//todo : signal renderAvailable semaphore
	
	//todo : GetStatus to do unblocking.
	vkWaitForFences(_device.GetDevice(), 1, &_inFlightFences[_currentFrame], VK_TRUE, 100000000000);

	auto deltaTime = static_cast<float>(_timer.tick<Core::Timer::Seconds>());
	cout << "Transfer Time : " << deltaTime << endl;
}
