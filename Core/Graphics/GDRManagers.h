#pragma once
#include "MeshBufferManager.h"
#include "MaterialManager.h"
#include "Vulkans/BindlessTextureManager.h"
#include "RendererBatch.h"

namespace Core
{
	struct GDRManagers
	{
		unique_ptr<BindlessTextureManager> Bindless;
		unique_ptr<MeshBufferManager> MeshBuffer;
		unique_ptr<MaterialManager> Material;
		unique_ptr<RendererBatch> Batch;
	};
}