#include "stdafx.h"
#include "TransferContext.h"
#include "TransferJob.h"
#include "Foundation/WorkerThread.h"
#include "Graphics/FrameCounter.h"
#include "Graphics/SyncContext.h"
#include "Graphics/Vulkans/CommandPool.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Buffer.h"

using namespace Core;

TransferContext::TransferContext(Device& device, WorkerThreadManager& workerThreadManager,
	SyncContext& syncContext)
	: _device(device)
	, _workerThreadManager(workerThreadManager)
	, _sync(syncContext)
{
	_primaryCommandPool = make_unique<CommandPool>(_device,
		_device.GetQueueFamilyIndices().TransferFamily.value());

	// Sized for steady-state traffic (terrain tiles, table refills); the
	// initial scene load intentionally overflows into per-job fallbacks.
	_stagingRing = make_unique<StagingRing>(_device, 32ull * 1024 * 1024);
}

TransferContext::~TransferContext() = default;

void TransferContext::BeginFrame()
{
	// Frame-slot spans stamped "safe at frame N" retire here: Begin's
	// in-flight wait (which precedes this) has retired that slot's frame.
	_stagingRing->Reclaim(_lastSubmittedValue, FrameCounter::GetFrameNumber());
	_stagingRing->BeginFrame();
}

VkDeviceSize TransferContext::GrantUploadBudget(VkDeviceSize requested)
{
	VkDeviceSize used = _stagingRing->GetFrameRequested();
	VkDeviceSize remaining = used < _uploadBudgetPerFrame
		? _uploadBudgetPerFrame - used : 0;
	return std::min(requested, remaining);
}

void TransferContext::OnGUI()
{
	// Appends to the engine-level "Status" window (same-name Begin appends).
	ImGui::Begin("Status");
	ImGui::Separator();
	ImGui::Text("Upload budget: %llu / %llu KB this frame",
		_stagingRing->GetFrameRequested() / 1024, _uploadBudgetPerFrame / 1024);
	ImGui::Text("Staging ring: %llu / %llu KB (peak %llu), fallbacks %u",
		_stagingRing->GetUsed() / 1024, _stagingRing->GetCapacity() / 1024,
		_stagingRing->GetPeakUsed() / 1024, _stagingRing->GetFallbackCount());
	ImGui::End();
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
			Enqueue(make_unique<VkImageJob>(_device, texture, request.filePath,
				_stagingRing.get()), texture.GetName());
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
			make_unique<VkBufferCopyBatchJob>(_device, move(copies), move(boundsTasks),
				_stagingRing.get()),
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
	primary.EndCommandBuffer();

	// The SyncContext owns the transfer timeline and remembers the value so
	// the frame's queue submits gate on it (pre-signalled while this stays
	// synchronous; load-bearing once the CPU wait below goes away).
	uint64_t signalValue = _sync.SubmitTransfer(primary.GetHandle());
	_lastSubmittedValue = signalValue;

	// Every span the jobs acquired belongs to this submission.
	_stagingRing->Stamp(signalValue);

	lock.unlock();

	// Still synchronous: block until this submission's value signals. The
	// async step replaces this with per-frame vkGetSemaphoreCounterValue
	// polling that promotes completed uploads instead of stalling here.
	VkSemaphore transferSemaphore = _sync.GetSemaphore(QueueType::Transfer);
	VkSemaphoreWaitInfo waitInfo{};
	waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
	waitInfo.semaphoreCount = 1;
	waitInfo.pSemaphores = &transferSemaphore;
	waitInfo.pValues = &signalValue;
	vkWaitSemaphores(_device.GetDevice(), &waitInfo, UINT64_MAX);

	_stagingRing->Reclaim(signalValue, FrameCounter::GetFrameNumber());

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
