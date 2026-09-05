#include "stdafx.h"
#include "TransferContext.h"

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
	: Threadable(workerThreadManager)
	, _device(device)
	, _sync(syncContext)
{
	_primaryCommandPool = make_unique<CommandPool>(_device, syncContext,
		QueueType::Transfer);
	_graphicsPrimaryCommandPool = make_unique<CommandPool>(_device, syncContext,
		QueueType::Graphics);

	// Sized for steady-state traffic (terrain tiles, table refills); the
	// initial scene load intentionally overflows into per-job fallbacks.
	_stagingRing = make_unique<StagingRing>(_device, 32ull * 1024 * 1024);
}

TransferContext::~TransferContext() = default;

void TransferContext::BeginFrame()
{
	CollectCompletedJobs();

	_stagingRing->Reclaim(_sync.GetCompletedValue(QueueType::Transfer),
		_sync.GetCompletedResourceValue());
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

void TransferContext::CollectCompletedJobs()
{
	const uint64_t transferCompleted = _sync.GetCompletedValue(QueueType::Transfer);
	const uint64_t graphicsLaneCompleted = _sync.GetCompletedResourceValue();

	while (!_inFlightJobs.empty())
	{
		const InFlightJobs& front = _inFlightJobs.front();
		const uint64_t completed = (front.lane == QueueType::Graphics)
			? graphicsLaneCompleted : transferCompleted;
		if (completed < front.value)
			break;

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

void TransferContext::SubmitJob(PendingUpload&& upload, const string& jobName)
{
	// Already enqueued: the pending upload covers the request; this one is
	// destroyed on return.
	if (_pendingUploads.find(jobName) != _pendingUploads.end())
		return;

	upload.job->type = (upload.lane == QueueType::Graphics)
		? JobType::GRAPHICS_SECONDARY : JobType::TRANSFER;

	upload.job->stagingRing = _stagingRing.get();

	Job& job = *upload.job;
	_pendingUploads.insert({ jobName, std::move(upload) });
	EnqueueUnowned(job);
}

void TransferContext::Flush(bool waitForRecordings)
{
	if (_pendingUploads.empty())
		return;

	_timer.tick();

	if (waitForRecordings)
	{
		// Graphics-lane (frame-coherent) uploads only: memcpy recordings that
		// finish in microseconds. IO-bound loads keep carrying over instead of
		// blocking the frame here.
		WaitFor([this]
		{
			for (auto&& upload : _pendingUploads)
			{
				if (upload.second.lane == QueueType::Graphics
					&& upload.second.job->status != JobStatus::COMPLETE)
				{
					return false;
				}
			}
			return true;
		});
	}

	// Submit what finished recording, one batch per lane; the rest (slow file
	// reads) stay pending and ride a later Flush instead of stalling this frame.
	InFlightJobs transferBatch, graphicsBatch;
	vector<CommandBuffer*> transferSecondaries, graphicsSecondaries;
	for (auto it = _pendingUploads.begin(); it != _pendingUploads.end();)
	{
		if (it->second.job->status == JobStatus::COMPLETE)
		{
			const bool graphicsLane = it->second.lane == QueueType::Graphics;
			(graphicsLane ? graphicsSecondaries : transferSecondaries)
				.push_back(it->second.job->commandBuffer);
			(graphicsLane ? graphicsBatch : transferBatch)
				.uploads.push_back(std::move(it->second));
			it = _pendingUploads.erase(it);
		}
		else
		{
			++it;
		}
	}

	SubmitBatch(std::move(transferBatch), transferSecondaries, QueueType::Transfer);
	SubmitBatch(std::move(graphicsBatch), graphicsSecondaries, QueueType::Graphics);

	auto deltaTime = static_cast<float>(_timer.tick<Core::Timer::Seconds>());
	cout << "Transfer Time : " << deltaTime << endl;
}

void TransferContext::SubmitBatch(InFlightJobs&& batch,
	vector<CommandBuffer*>& secondaries, QueueType lane)
{
	if (batch.uploads.empty())
		return;

	CommandPool& pool = (lane == QueueType::Graphics)
		? *_graphicsPrimaryCommandPool : *_primaryCommandPool;

	auto& primary = pool.RequestCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	primary.BeginCommandBuffer(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
	primary.ExecuteCommands(secondaries);
	primary.EndCommandBuffer();

	uint64_t signalValue = (lane == QueueType::Graphics)
		? _sync.SubmitGraphicsUpload(primary, secondaries)
		: _sync.SubmitTransfer(primary, secondaries);
	batch.value = signalValue;
	batch.lane = lane;

	// Close each submitted upload's staging span on THIS submission's value;
	// spans of still-recording jobs stay open and block ring reclamation
	// behind them until their own submission closes them.
	for (PendingUpload& upload : batch.uploads)
	{
		if (upload.job->stagingSpanId != UINT64_MAX)
			_stagingRing->Close(upload.job->stagingSpanId, signalValue, lane);
	}

	// The uploads (owning any fallback staging) stay alive until BeginFrame's
	// completion poll sees the GPU pass this value.
	_inFlightJobs.push_back(std::move(batch));
}


