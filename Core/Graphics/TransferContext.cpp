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

void TransferContext::Update()
{
	BeginFrame();
	SubmitQueued();
	Flush();
}

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

void TransferContext::EnqueueUpload(PendingUpload&& upload, const string& jobName)
{
	// Already enqueued: the pending upload covers the request; this one is
	// destroyed on return.
	if (_pendingJobs.find(jobName) != _pendingJobs.end())
		return;

	upload.round = _submitRound;

	Job* raw = upload.job.get();
	raw->completionWait = &_jobWait;

	_pendingJobs.insert({ jobName, std::move(upload) });
	_workerThreadManager.Enqueue(raw);
}

void TransferContext::SubmitQueued()
{
	++_submitRound;

	if (!_textureUploads.Empty())
	{
		// The job takes the resolved Texture&; handles resolve here on the
		// main thread (the pool is not thread-safe).
		for (auto& request : _textureUploads.Take())
		{
			auto& texture = request.texture.Get();
			string jobName = texture.GetName();

			PendingUpload upload;
			upload.texture = request.texture;
			upload.job = make_unique<TextureUploadJob>(_device, texture,
				request.filePath, _stagingRing.get());
			EnqueueUpload(std::move(upload), jobName);
		}
	}

	if (_geometryCopies.Empty())
		return;

	// One upload job per batch (i.e. per submesh); the job consumes the
	// request whole and resolves its destination handles itself.
	size_t batchIndex = 0;
	for (auto& batch : _geometryCopies.Take())
	{
		PendingUpload upload;
		upload.subMesh = batch.subMesh;

		string jobName = batch.debugName + "_" + std::to_string(batchIndex);
		upload.job = make_unique<GeometryUploadJob>(_device, move(batch),
			_stagingRing.get());
		EnqueueUpload(std::move(upload), jobName);

		++batchIndex;
	}
}

void TransferContext::Flush(bool waitForRecordings)
{
	if (_pendingJobs.empty())
		return;

	_timer.tick();

	unique_lock<mutex> lock(_lock);
	if (waitForRecordings)
	{
		// Only the latest round: this frame's must-land uploads (table fills)
		// finish in microseconds; an older round's slow file read keeps
		// carrying over instead of re-blocking the frame here.
		_jobWait.wait(lock, [&]
		{
			for (auto&& upload : _pendingJobs)
			{
				if (upload.second.round == _submitRound
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


