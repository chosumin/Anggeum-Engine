#include "stdafx.h"
#include "RenderScene.h"
#include "Graphics/Vulkans/Device.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Buffer.h"
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
	// Enqueue all pending uploads first. The mesh buffer manager only allocates; its
	// copies join the same buffer-upload path as standalone buffers, so every transfer
	// job is issued here (in UploadQueued*), never inside a manager.
	UploadQueuedTextures(transfer);

	for (auto& upload : _meshBuffer->Sync())
		_bufferUploads.Push(move(upload));

	UploadQueuedBuffers(transfer);

	// ...and flush them before the steps below: the bindless descriptor writes need
	// the uploaded textures' image views (created by the image jobs), and the draw
	// set references the uploaded geometry.
	transfer.Wait();

	if (_bindless)
		_bindless->Sync();

	_material->Sync();
	_batch->Sync(scene, extents);
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
		transfer.Enqueue(new VkImageJob(*_device, texture, request.filePath),
			texture.GetName());
	}
}

void RenderScene::UploadQueuedBuffers(TransferContext& transfer)
{
	if (_bufferUploads.Empty())
		return;

	size_t uploadIndex = 0;
	for (auto& upload : _bufferUploads.Take())
	{
		vector<BufferCopyRegion> copies;
		copies.reserve(upload.regions.size());

		for (auto& region : upload.regions)
			copies.push_back({ &region.buffer.Get(), move(region.data), region.offset });

		transfer.Enqueue(new VkBufferCopyBatchJob(*_device, move(copies)),
			"BufferUpload_" + upload.debugName + "_" + std::to_string(uploadIndex));

		++uploadIndex;
	}
}
