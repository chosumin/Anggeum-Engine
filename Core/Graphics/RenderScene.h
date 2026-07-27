#pragma once
#include "MeshBufferManager.h"
#include "MaterialManager.h"
#include "Vulkans/BindlessTextureManager.h"
#include "RendererBatch.h"
#include "GeometryUpload.h"

namespace Core
{
	class Device;
	class Scene;
	class TransferContext;

	// RenderScene: the GPU mirror of the scene used for GPU-driven rendering (bindless
	// textures, mesh buffers, material table, draw batch). Owned by Engine and shared by
	// reference with RenderContext / RenderFrames. Sync() pushes scene changes to the
	// GPU and is driven by the scene's dirty state (see Engine::Draw).
	struct RenderScene
	{
		unique_ptr<BindlessTextureManager> Bindless;
		unique_ptr<MeshBufferManager> MeshBuffer;
		unique_ptr<MaterialManager> Material;
		unique_ptr<RendererBatch> Batch;

		// Where loaders drop raw geometry; drained by Sync (see GeometryUploadQueue).
		GeometryUploadQueue GeometryUploads;

		// Default: all managers null (the empty sentinel temp frames bind to).
		RenderScene() = default;
		explicit RenderScene(Device& device);

		// Flush pending GPU updates. Bindless/material self-gate on their own pending
		// state (e.g. IBL textures register outside scene structure changes) and run
		// every frame. On scene-dirty, uploads queued geometry (via `transfer`) and
		// rebuilds the draw set.
		void Sync(Scene& scene, TransferContext& transfer, VkExtent2D extents);

	private:
		void UploadQueuedGeometry(TransferContext& transfer);

		// Non-owning; null for the default (empty) instance, which never syncs.
		Device* _device = nullptr;
	};
}
