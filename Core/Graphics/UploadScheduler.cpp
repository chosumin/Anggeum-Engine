#include "stdafx.h"
#include "UploadScheduler.h"
#include "TransferContext.h"
#include "TransferJob.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Buffer.h"

using namespace Core;

UploadScheduler::UploadScheduler(Device& device, TransferContext& transfer)
	: _device(device)
	, _transfer(transfer)
{
}

void UploadScheduler::SubmitQueued()
{
	if (!_textureUploads.Empty())
	{
		// The job reads the file on a worker thread; resolve the handle here on
		// the main thread (the pool is not thread-safe).
		for (auto& request : _textureUploads.Take())
		{
			auto& texture = request.texture.Get();
			_transfer.Enqueue(make_unique<VkImageJob>(_device, texture, request.filePath),
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

		_transfer.Enqueue(
			make_unique<VkBufferCopyBatchJob>(_device, move(copies), move(boundsTasks)),
			batch.debugName + "_" + std::to_string(batchIndex));

		++batchIndex;
	}
}

void UploadScheduler::Flush()
{
	_transfer.Wait();
}

vector<UploadScheduler::CompletedBounds> UploadScheduler::TakeCompletedBounds()
{
	// Flush() has completed the jobs, so every result is safe to read.
	vector<CompletedBounds> completed;
	completed.reserve(_pendingBounds.size());
	for (auto& pending : _pendingBounds)
		completed.push_back({ pending.target, *pending.result });

	_pendingBounds.clear();
	return completed;
}
