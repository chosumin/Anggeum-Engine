#include "stdafx.h"
#include "TransferContext.h"
#include "TransferJob.h"
#include "Foundation/WorkerThread.h"
#include "Graphics/Vulkans/CommandPool.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Buffer.h"

using namespace Core;

TransferContext::TransferContext(Device& device, WorkerThreadManager& workerThreadManager)
	: _device(device)
	, _workerThreadManager(workerThreadManager)
{
	VkSemaphoreTypeCreateInfo semaphoreTypeInfo{};
	semaphoreTypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
	semaphoreTypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;

	VkSemaphoreCreateInfo semaphoreInfo{};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreInfo.pNext = &semaphoreTypeInfo;

	if (vkCreateSemaphore(_device.GetDevice(), &semaphoreInfo, nullptr, &_timeline) != VK_SUCCESS)
		throw runtime_error("failed to create the upload timeline semaphore!");

	_primaryCommandPool = make_unique<CommandPool>(_device,
		_device.GetQueueFamilyIndices().TransferFamily.value());
}

TransferContext::~TransferContext()
{
	vkDestroySemaphore(_device.GetDevice(), _timeline, nullptr);
}

void TransferContext::Enqueue(unique_ptr<Job> job, const string& jobName)
{
	// Already enqueued: the pending job covers the request; this one is destroyed.
	if (_pendingJobs.find(jobName) != _pendingJobs.end())
		return;

	Job* raw = job.get();
	raw->completionWait = &_jobWait;

	_pendingJobs.insert({ jobName, std::move(job) });
	_workerThreadManager.Enqueue(raw);
}

void TransferContext::SubmitQueued()
{
	if (!_textureUploads.Empty())
	{
		// The job reads the file on a worker thread; resolve the handle here on
		// the main thread (the pool is not thread-safe).
		for (auto& request : _textureUploads.Take())
		{
			auto& texture = request.texture.Get();
			Enqueue(make_unique<VkImageJob>(_device, texture, request.filePath),
				texture.GetName());
		}
	}

	if (_geometryCopies.Empty())
		return;

	// One transfer job per batch (i.e. per submesh).
	size_t batchIndex = 0;
	for (auto& batch : _geometryCopies.Take())
	{
		vector<BufferCopyRegion> copies;
		copies.reserve(batch.copies.size());

		vector<BoundsTask> boundsTasks;

		for (auto& copy : batch.copies)
		{
			// The job computes bounds from the POSITION stream while it holds the data.
			if (copy.boundsStride > 0 && batch.boundsTarget)
			{
				auto result = make_unique<GeometryBounds>();
				boundsTasks.push_back({ copies.size(), copy.boundsStride, result.get() });
				_pendingBounds.push_back({ batch.boundsTarget, move(result) });
			}

			copies.push_back({ &copy.destination.Get(), move(copy.data), copy.offset });
		}

		Enqueue(
			make_unique<VkBufferCopyBatchJob>(_device, move(copies), move(boundsTasks)),
			batch.debugName + "_" + std::to_string(batchIndex));

		++batchIndex;
	}
}

void TransferContext::Flush()
{
	if (_pendingJobs.empty())
		return;

	_timer.tick();

	// Every job records its copy commands on a worker thread; wait for the
	// recordings, then execute them all as one submit.
	unique_lock<mutex> lock(_lock);
	_jobWait.wait(lock, [&]
	{
		for (auto&& jobs : _pendingJobs)
		{
			if (jobs.second->status != JobStatus::COMPLETE)
			{
				return false;
			}
		}
		return true;
	});

	auto& primary = _primaryCommandPool->RequestCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	primary.BeginCommandBuffer(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	size_t commandBufferCount = _pendingJobs.size();
	vector<CommandBuffer*> secondaryCommands(commandBufferCount);
	transform(_pendingJobs.begin(), _pendingJobs.end(),
		secondaryCommands.begin(),
		[](const auto& job) { return job.second->commandBuffer; });

	primary.ExecuteCommands(secondaryCommands);

	uint64_t signalValue = ++_submittedValue;

	VkTimelineSemaphoreSubmitInfo timelineInfo{};
	timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
	timelineInfo.signalSemaphoreValueCount = 1;
	timelineInfo.pSignalSemaphoreValues = &signalValue;

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.pNext = &timelineInfo;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &primary.GetHandle();
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &_timeline;

	primary.EndCommandBuffer();

	VkQueue queue = _device.GetTransferQueue();
	vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);

	lock.unlock();

	// Still synchronous: block until this submission's value signals. The
	// async step replaces this with per-frame vkGetSemaphoreCounterValue
	// polling that promotes completed uploads instead of stalling here.
	VkSemaphoreWaitInfo waitInfo{};
	waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
	waitInfo.semaphoreCount = 1;
	waitInfo.pSemaphores = &_timeline;
	waitInfo.pValues = &signalValue;
	vkWaitSemaphores(_device.GetDevice(), &waitInfo, UINT64_MAX);

	ClearJobs();

	auto deltaTime = static_cast<float>(_timer.tick<Core::Timer::Seconds>());
	cout << "Transfer Time : " << deltaTime << endl;
}

vector<TransferContext::CompletedBounds> TransferContext::TakeCompletedBounds()
{
	// Flush() has completed the jobs, so every result is safe to read.
	vector<CompletedBounds> completed;
	completed.reserve(_pendingBounds.size());
	for (auto& pending : _pendingBounds)
		completed.push_back({ pending.target, *pending.result });

	_pendingBounds.clear();
	return completed;
}

void TransferContext::ClearJobs()
{
	// Flush() only reaches here after every job reported COMPLETE and the GPU fence
	// signaled, so destroying them (and the staging buffers they own) is safe.
	_pendingJobs.clear();
}
