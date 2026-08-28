#include "stdafx.h"
#include "AssetStreamer.h"

#include "Graphics/TransferContext.h"
#include "Graphics/ResourcePool.h"
#include "Graphics/SubMesh.h"
#include "Graphics/Vulkans/Texture.h"

using namespace Core;

AssetStreamer::AssetStreamer(Device& device, TransferContext& transfer)
	: _device(device)
	, _transfer(transfer)
{
}

void AssetStreamer::SubmitQueued()
{
	// Admission control, FIFO: each request charges the shared frame budget
	// before it becomes a job; the first one the budget cannot cover stops
	// the drain, and everything behind it retries next frame in order.
	while (!_textureUploads.Empty())
	{
		if (!_transfer.TryAdmit(_textureUploads.Front().stagingBytes))
			break;

		// The job takes the resolved Texture&; handles resolve here on the
		// main thread (the pool is not thread-safe).
		TextureUploadRequest request = _textureUploads.PopFront();
		auto& texture = request.texture.Get();
		string jobName = texture.GetName();

		TransferContext::PendingUpload upload;
		upload.texture = request.texture;
		upload.job = make_unique<TextureUploadJob>(_device, texture,
			request.filePath);
		_transfer.SubmitJob(std::move(upload), jobName);
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
			if (!_transfer.TryAdmit(bytes))
			{
				deferred.push_back(std::move(batch));
				continue;
			}
		}

		TransferContext::PendingUpload upload;
		upload.subMesh = batch.subMesh;
		// Table-class = must-land (see the budget bypass above); its recording
		// is a memcpy of CPU-built tables.
		upload.mustLand = !batch.subMesh.IsValid();

		string jobName = batch.debugName + "_" + std::to_string(batchIndex);
		upload.job = make_unique<GeometryUploadJob>(_device, move(batch));
		_transfer.SubmitJob(std::move(upload), jobName);

		++batchIndex;
	}

	for (auto& batch : deferred)
		_geometryCopies.Push(std::move(batch));
}
