#include "stdafx.h"
#include "TransferContext.h"

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
	// here - transfer-stamped ring spans and the jobs (and fallback staging)
	// of completed batches.
	uint64_t completed = _sync.QueryCompletedValue(QueueType::Transfer);
	CollectCompletedJobs(completed);
	_stagingRing->Reclaim(completed);
	_stagingRing->BeginFrame();
	_frameAdmittedBytes = 0;
}

bool TransferContext::TryAdmit(VkDeviceSize bytes)
{
	// The oversized-resource exception: better one over-budget frame than a
	// request that can never be admitted.
	if (_frameAdmittedBytes == 0 && bytes > _uploadBudgetPerFrame)
	{
		_frameAdmittedBytes += bytes;
		return true;
	}

	if (_frameAdmittedBytes + bytes > _uploadBudgetPerFrame)
		return false;

	_frameAdmittedBytes += bytes;
	return true;
}

void TransferContext::CollectCompletedJobs(uint64_t completedValue)
{
	while (!_inFlightJobs.empty() && _inFlightJobs.front().value <= completedValue)
	{
		// Activate-on-completion, per upload: residency flips and bounds
		// deliver exactly when this upload's data is known to be on the GPU.
		for (PendingUpload& upload : _inFlightJobs.front().uploads)
		{
			if (upload.texture.IsValid())
			{
				upload.texture.SetResident();
				++_promotedCount;
			}
			if (upload.subMesh.IsValid())
			{
				upload.subMesh.SetResident();
				++_promotedCount;
			}
		}

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
	VkDeviceSize remaining = _frameAdmittedBytes < _uploadBudgetPerFrame
		? _uploadBudgetPerFrame - _frameAdmittedBytes : 0;
	VkDeviceSize granted = std::min(requested, remaining);
	_frameAdmittedBytes += granted;
	return granted;
}

void TransferContext::OnGUI()
{
	// Appends to the engine-level "Status" window (same-name Begin appends).
	ImGui::Begin("Status");
	ImGui::Separator();
	ImGui::Text("Upload budget: %llu / %llu KB admitted (staged %llu)",
		_frameAdmittedBytes / 1024, _uploadBudgetPerFrame / 1024,
		_stagingRing->GetFrameRequested() / 1024);
	ImGui::Text("Staging ring: %llu / %llu KB (peak %llu), fallbacks %u",
		_stagingRing->GetUsed() / 1024, _stagingRing->GetCapacity() / 1024,
		_stagingRing->GetPeakUsed() / 1024, _stagingRing->GetFallbackCount());
	ImGui::End();
}

void TransferContext::SubmitStreamingJob(unique_ptr<UploadJob> job, const string& jobName)
{
	PendingUpload upload;
	upload.job = std::move(job);
	upload.mustLand = true;
	EnqueueUpload(std::move(upload), jobName);
}

void TransferContext::EnqueueUpload(PendingUpload&& upload, const string& jobName)
{
	// Already enqueued: the pending upload covers the request; this one is
	// destroyed on return.
	if (_pendingJobs.find(jobName) != _pendingJobs.end())
		return;

	Job* raw = upload.job.get();
	raw->completionWait = &_jobWait;

	_pendingJobs.insert({ jobName, std::move(upload) });
	_workerThreadManager.Enqueue(raw);
}

void TransferContext::SubmitQueued()
{
	// Admission control, FIFO: each request charges the shared frame budget
	// before it becomes a job; the first one the budget cannot cover stops
	// the drain, and everything behind it retries next frame in order.
	while (!_textureUploads.Empty())
	{
		if (!TryAdmit(_textureUploads.Front().stagingBytes))
			break;

		// The job takes the resolved Texture&; handles resolve here on the
		// main thread (the pool is not thread-safe).
		TextureUploadRequest request = _textureUploads.PopFront();
		auto& texture = request.texture.Get();
		string jobName = texture.GetName();

		PendingUpload upload;
		upload.texture = request.texture;
		upload.job = make_unique<TextureUploadJob>(_device, texture,
			request.filePath, _stagingRing.get());
		EnqueueUpload(std::move(upload), jobName);
	}

	// One upload job per batch (i.e. per submesh); the job consumes the
	// request whole and resolves its destination handles itself.
	//
	// table-class batches (no submesh handle - draw-set tables,
	// the terrain grid) bypass the budget and MUST drain the frame they were
	// pushed. A table batch carried across a rebuild would record into the
	// very buffers the rebuild replaces.
	vector<GeometryCopyBatch> deferred;
	size_t batchIndex = 0;
	while (!_geometryCopies.Empty())
	{
		GeometryCopyBatch batch = _geometryCopies.PopFront();

		if (batch.subMesh.IsValid())
		{
			VkDeviceSize bytes = 0;
			for (const auto& copy : batch.copies)
				bytes += copy.data.size();

			// Unaffordable batches are SKIPPED (kept in order for next frame)
			if (!TryAdmit(bytes))
			{
				deferred.push_back(std::move(batch));
				continue;
			}
		}

		PendingUpload upload;
		upload.subMesh = batch.subMesh;
		// Table-class = must-land (see the budget bypass above); its recording
		// is a memcpy of CPU-built tables.
		upload.mustLand = !batch.subMesh.IsValid();

		string jobName = batch.debugName + "_" + std::to_string(batchIndex);
		upload.job = make_unique<GeometryUploadJob>(_device, move(batch),
			_stagingRing.get());
		EnqueueUpload(std::move(upload), jobName);

		++batchIndex;
	}

	for (auto& batch : deferred)
		_geometryCopies.Push(std::move(batch));
}

void TransferContext::Flush(bool waitForRecordings)
{
	if (_pendingJobs.empty())
		return;

	_timer.tick();

	unique_lock<mutex> lock(_lock);
	if (waitForRecordings)
	{
		// Must-land uploads only: memcpy recordings that finish in microseconds. 
		// IO-bound loads keep carrying over instead of blocking the frame here.
		_jobWait.wait(lock, [&]
		{
			for (auto&& upload : _pendingJobs)
			{
				if (upload.second.mustLand
					&& upload.second.job->status != JobStatus::COMPLETE)
				{
					return false;
				}
			}
			return true;
		});
	}

	// Submit what finished recording; the rest (slow file reads) stay pending
	// and ride a later Flush instead of stalling this frame.
	InFlightJobs batch;
	vector<CommandBuffer*> secondaryCommands;
	for (auto it = _pendingJobs.begin(); it != _pendingJobs.end();)
	{
		if (it->second.job->status == JobStatus::COMPLETE)
		{
			secondaryCommands.push_back(it->second.job->commandBuffer);
			batch.uploads.push_back(std::move(it->second));
			it = _pendingJobs.erase(it);
		}
		else
		{
			++it;
		}
	}

	if (batch.uploads.empty())
		return;

	auto& primary = _primaryCommandPool->RequestCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	primary.BeginCommandBuffer(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	primary.ExecuteCommands(secondaryCommands);
	primary.EndCommandBuffer();

	uint64_t signalValue = _sync.SubmitTransfer(primary.GetHandle());
	batch.value = signalValue;

	// Close each submitted upload's staging span on THIS submission's value;
	// spans of still-recording jobs stay open and block ring reclamation
	// behind them until their own submission closes them.
	for (PendingUpload& upload : batch.uploads)
	{
		if (upload.job->stagingSpanId != UINT64_MAX)
			_stagingRing->Close(upload.job->stagingSpanId, signalValue);
	}

	lock.unlock();

	// The uploads (owning any fallback staging) stay alive until BeginFrame's
	// completion poll sees the GPU pass this value.
	_inFlightJobs.push_back(std::move(batch));

	auto deltaTime = static_cast<float>(_timer.tick<Core::Timer::Seconds>());
	cout << "Transfer Time : " << deltaTime << endl;
}


