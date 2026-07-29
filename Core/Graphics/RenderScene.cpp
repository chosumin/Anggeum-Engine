#include "stdafx.h"
#include "RenderScene.h"
#include "Graphics/Vulkans/Device.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/SubMesh.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/TransferContext.h"
#include "Graphics/TransferJob.h"
#include "Foundation/Scene.h"

using namespace Core;

RenderScene::RenderScene(Device& device)
	: _device(&device)
{
	// Bindless textures require descriptor indexing; the rest are always created.
	if (device.SupportsDescriptorIndexing())
		_bindless = make_unique<BindlessTextureManager>(device, 4096);

	_meshBuffer = make_unique<MeshBufferManager>(device);
	_material = make_unique<MaterialManager>(device);
	_batch = make_unique<RendererBatch>(device);
}

void RenderScene::Sync(Scene& scene, TransferContext& transfer, VkExtent2D extents)
{
	// Every manager self-gates on its own dirty state; this only fixes the order.
	// Space was already reserved at load time, so this just enqueues the pending data
	// copies — every transfer job is issued here.
	UploadQueuedTextures(transfer);
	UploadQueuedGeometry(transfer);

	// ...and flush them before the steps below: the bindless descriptor writes need
	// the uploaded textures' image views (created by the image jobs), and the draw
	// set references the uploaded geometry and the bounds those jobs computed.
	transfer.Wait();

	ApplyComputedBounds();

	if (_bindless)
		_bindless->Sync();

	_material->Sync();

	// The batch enqueues its own buffer fills rather than submitting them, so flush
	// once more: the passes read the draw set on the GPU during this frame.
	_batch->Sync(scene, transfer, extents);
	transfer.Wait();
}

void RenderScene::UploadQueuedTextures(TransferContext& transfer)
{
	if (_textureUploads.Empty())
		return;

	// The job reads the file on a worker thread; resolve the handle here on the main
	// thread (the pool is not thread-safe).
	for (auto& request : _textureUploads.Take())
	{
		auto& texture = request.texture.Get();
		transfer.Enqueue(make_unique<VkImageJob>(*_device, texture, request.filePath),
			texture.GetName());
	}
}

void RenderScene::UploadQueuedGeometry(TransferContext& transfer)
{
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

		transfer.Enqueue(
			make_unique<VkBufferCopyBatchJob>(*_device, move(copies), move(boundsTasks)),
			batch.debugName + "_" + std::to_string(batchIndex));

		++batchIndex;
	}
}

void RenderScene::ApplyComputedBounds()
{
	// Called once the upload jobs are done, so each result is safe to read here.
	for (auto& pending : _pendingBounds)
	{
		auto& bounds = *pending.result;
		pending.target->SetBoundingSphere(bounds.center, bounds.radius);

		_sceneBoundsMin = glm::min(_sceneBoundsMin, bounds.min);
		_sceneBoundsMax = glm::max(_sceneBoundsMax, bounds.max);
	}

	_pendingBounds.clear();
}
