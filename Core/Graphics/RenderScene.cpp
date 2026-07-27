#include "stdafx.h"
#include "RenderScene.h"
#include "Graphics/Vulkans/Device.h"
#include "Graphics/SubMesh.h"
#include "Graphics/TransferContext.h"
#include "Graphics/TransferJob.h"
#include "Foundation/Scene.h"

using namespace Core;

RenderScene::RenderScene(Device& device)
	: _device(&device)
{
	// Bindless textures require descriptor indexing; the rest are always created.
	if (device.SupportsDescriptorIndexing())
		Bindless = make_unique<BindlessTextureManager>(device, 4096);

	MeshBuffer = make_unique<MeshBufferManager>(device);
	Material = make_unique<MaterialManager>(device);
	Batch = make_unique<RendererBatch>(device);
}

void RenderScene::Sync(Scene& scene, TransferContext& transfer, VkExtent2D extents)
{
	// Bindless/material only do work when they have pending changes (new textures,
	// dirty materials), so they are safe to poll every frame.
	if (Bindless)
		Bindless->UpdateDescriptorSet();

	Material->RefreshDirtyMaterials();

	if (!scene.IsDirty())
		return;

	// A mesh renderer was added/removed: upload any new geometry, then rebuild the
	// (expensive) draw set. The caller waits on `transfer` before rendering this frame.
	UploadQueuedGeometry(transfer);
	Batch->Prepare(scene, extents);
}

void RenderScene::UploadQueuedGeometry(TransferContext& transfer)
{
	// Drain the queue loaders filled: reserve space in the global mesh buffers and
	// enqueue one transfer job per source mesh.
	auto uploads = GeometryUploads.Take();

	size_t meshIndex = 0;
	for (auto& upload : uploads)
	{
		vector<BufferCopyRegion> meshCopies;

		for (auto& geometry : upload.subMeshes)
		{
			auto& sm = geometry.subMesh.Get();
			if (sm.HasAllocation())
				continue;

			// One span per submesh, shared across its vertex attributes (see
			// MeshBufferManager::Allocate); Build() commits it.
			for (auto& attr : geometry.attributes)
			{
				auto region = MeshBuffer->Allocate(attr.name, attr.stride, attr.data);
				meshCopies.push_back({ region.destination, move(attr.data), region.offset });
			}

			if (geometry.hasIndex)
			{
				auto region = MeshBuffer->Allocate(geometry.indexType, geometry.indexData);
				meshCopies.push_back({ region.destination, move(geometry.indexData), region.offset });
			}

			sm.SetAllocation(MeshBuffer->Build());
		}

		if (!meshCopies.empty())
		{
			transfer.Enqueue(
				new VkBufferCopyBatchJob(*_device, move(meshCopies)),
				"GeometryUpload_" + upload.debugName + "_" + std::to_string(meshIndex));
		}

		++meshIndex;
	}
}
