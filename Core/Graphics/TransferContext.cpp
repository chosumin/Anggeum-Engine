#include "stdafx.h"
#include "TransferContext.h"
#include "TransferJob.h"
#include "Foundation/WorkerThread.h"
#include "Graphics/FrameCounter.h"
#include "Graphics/SyncContext.h"
#include "Graphics/ResourcePool.h"
#include "Graphics/SubMesh.h"
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
	// Non-blocking completion poll: everything the GPU has passed retires
	// here - transfer-stamped ring spans, the jobs (and fallback staging)
	// of completed batches, and frame-slot spans whose frame Begin's
	// in-flight wait (which precedes this) has retired.
	uint64_t completed = _sync.QueryCompletedValue(QueueType::Transfer);
	CollectCompletedJobs(completed);
	_stagingRing->Reclaim(completed, FrameCounter::GetFrameNumber());
	_stagingRing->BeginFrame();
}

void TransferContext::CollectCompletedJobs(uint64_t completedValue)
{
	while (!_inFlightJobs.empty() && _inFlightJobs.front().value <= completedValue)
	{
		InFlightJobs& done = _inFlightJobs.front();
		for (auto& texture : done.promotions.textures)
			texture.SetResident();
		for (auto& subMesh : done.promotions.subMeshes)
			subMesh.SetResident();
		_promotedCount += uint32_t(done.promotions.textures.size()
			+ done.promotions.subMeshes.size());

		_inFlightJobs.pop_front();
	}
}

uint32_t TransferContext::TakePromotedCount()
{
	uint32_t count = _promotedCount;
	_promotedCount = 0;
	return count;
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
			_pendingPromotions.textures.push_back(request.texture);
		}
	}

	if (_geometryCopies.Empty())
		return;

	// One transfer job per batch (i.e. per submesh).
	size_t batchIndex = 0;
	for (auto& batch : _geometryCopies.Take())
	{
		if (batch.subMesh.IsValid())
			_pendingPromotions.subMeshes.push_back(batch.subMesh);

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

	uint64_t signalValue = _sync.SubmitTransfer(primary.GetHandle());

	// Every span the jobs acquired belongs to this submission.
	_stagingRing->Stamp(signalValue);

	lock.unlock();

	// The jobs (owning any fallback staging) stay alive until BeginFrame's
	// completion poll sees the GPU pass this value.
	InFlightJobs batch;
	batch.value = signalValue;
	batch.jobs.reserve(_pendingJobs.size());
	for (auto& [name, job] : _pendingJobs)
		batch.jobs.push_back(std::move(job));
	_pendingJobs.clear();
	batch.promotions = std::move(_pendingPromotions);
	_pendingPromotions = {};
	_inFlightJobs.push_back(std::move(batch));

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

