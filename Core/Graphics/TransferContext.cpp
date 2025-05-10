#include "stdafx.h"
#include "TransferContext.h"
#include "Foundation/WorkerThread.h"
#include "Graphics/Vulkans/CommandPool.h"
#include "Graphics/Vulkans/CommandBuffer.h"

Core::TransferContext::TransferContext(Device& device, WorkerThreadManager& workerThreadManager)
	:_device(device), _workerThreadManager(workerThreadManager)
{
	_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		if (vkCreateFence(_device.GetDevice(), &fenceInfo, nullptr, &_inFlightFences[i]) != VK_SUCCESS)
			throw runtime_error("failed to create semaphores!");
	}

	_primaryCommandPool = new CommandPool(_device,
		_device.GetQueueFamilyIndices().TransferFamily.value());
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
}

void Core::TransferContext::Enqueue(Job* job)
{
	_pendingJobs.push_back(job);
	job->completionWait = &_fenceWait;
	_workerThreadManager.Enqueue(job);
}

void Core::TransferContext::Wait()
{
	if (_pendingJobs.empty())
		return;

	_timer.tick();

	//todo : wait jobs instead of threads.
	unique_lock<mutex> lock(_lock);
	_fenceWait.wait(lock, [&]
	{
		for (size_t i = 0; i < _pendingJobs.size(); i++)
		{
			if (_pendingJobs[i]->status != JobStatus::COMPLETE)
			{
				return false;
			}
		}
		return true;
	});

	vkResetFences(_device.GetDevice(), 1, &_inFlightFences[_currentFrame]);

	auto& primary = _primaryCommandPool->RequestCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	primary.BeginCommandBuffer(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		nullptr, nullptr, 0, _currentFrame);

	size_t commandBufferCount = _pendingJobs.size();
	vector<CommandBuffer*> secondaryCommands(commandBufferCount);
	transform(_pendingJobs.begin(), _pendingJobs.end(), 
		secondaryCommands.begin(),
		[](Job* job) { return job->commandBuffer; });

	primary.ExecuteCommands(secondaryCommands);

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &primary.GetHandle();

	primary.EndCommandBuffer();

	VkQueue queue = _device.GetTransferQueue();
	vkQueueSubmit(queue, 1, &submitInfo, _inFlightFences[_currentFrame]);

	lock.unlock();

	//todo : GetStatus to do unblocking.
	vkWaitForFences(_device.GetDevice(), 1, &_inFlightFences[_currentFrame], VK_TRUE, 100000000000);

	ClearJobs();

	auto deltaTime = static_cast<float>(_timer.tick<Core::Timer::Seconds>());
	cout << "Transfer Time : " << deltaTime << endl;
}

void Core::TransferContext::ClearJobs()
{
	for (auto&& job : _pendingJobs)
	{
		if (job->status == JobStatus::COMPLETE)
		{
			delete(job);
		}
	}

	_pendingJobs.clear();
}
