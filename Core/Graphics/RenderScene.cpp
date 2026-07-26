#include "stdafx.h"
#include "RenderScene.h"
#include "Graphics/Vulkans/Device.h"
#include "Foundation/Scene.h"

using namespace Core;

RenderScene::RenderScene(Device& device)
{
	// Bindless textures require descriptor indexing; the rest are always created.
	if (device.SupportsDescriptorIndexing())
		Bindless = make_unique<BindlessTextureManager>(device, 4096);

	MeshBuffer = make_unique<MeshBufferManager>(device);
	Material = make_unique<MaterialManager>(device);
	Batch = make_unique<RendererBatch>(device);
}

void RenderScene::Sync(Scene& scene, VkExtent2D extents)
{
	// Bindless/material only do work when they have pending changes (new textures,
	// dirty materials), so they are safe to poll every frame.
	if (Bindless)
		Bindless->UpdateDescriptorSet();

	Material->RefreshDirtyMaterials();

	// Rebuilding the draw set is expensive (recompacts + reuploads GPU buffers), so
	// only do it when the scene structure changed.
	if (scene.IsDirty())
		Batch->Prepare(scene, extents);
}
