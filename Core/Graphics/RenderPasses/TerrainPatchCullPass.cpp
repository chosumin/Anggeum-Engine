#include "stdafx.h"
#include "TerrainPatchCullPass.h"
#include "TerrainNodeListPass.h"
#include "TerrainLodMapPass.h"
#include "HiZCullPass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/FrameResources.h"
#include "Graphics/RenderScene.h"
#include "Graphics/RenderContext.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/Terrain/TerrainSystem.h"
#include "Foundation/Scene.h"
#include "Components/PerspectiveCamera.h"
#include "Utils/Math.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Image.h"
#include "Graphics/Vulkans/Buffer.h"

using namespace Core;

TerrainPatchCullPass::TerrainPatchCullPass(Device& device, RenderScene& renderScene,
	VkExtent2D screenExtent)
	: _renderScene(renderScene)
	, _terrain(renderScene.GetTerrainSystem())
	, _screenExtent(screenExtent)
{
	auto& resourceManager = device.GetResourceManager();
	_shader = resourceManager.LoadShader("Shaders/Terrain/terrainPatchCull.comp.spv");
	_pipeline = resourceManager.LoadComputePipeline("Shaders/Terrain/terrainPatchCull.comp.spv");

	assert(_terrain.GetConfig().patchesPerNodeEdge == 8
		&& "terrainPatchCull.comp PATCHES_PER_EDGE / local_size mirror this");
}

void TerrainPatchCullPass::Setup(FrameGraphBuilder& builder,
	FrameResources& frameResources, RenderFrame& renderFrame)
{
	_active = false;
	_occlusionEnabled = false;

	PerspectiveCamera* camera = _renderScene.GetScene().GetMainCamera();
	if (!camera || !builder.HasBuffer(TerrainNodeListPass::SB_NODE_LIST))
		return;

	const TerrainConfig& config = _terrain.GetConfig();
	_push.cameraXZ = vec2(camera->Matrices.Position.x, camera->Matrices.Position.z);
	_push.worldOrigin = config.WorldOrigin();
	_push.rootNodeSize = config.rootNodeSize;
	_push.ringRadiusScale = config.ringRadiusScale;
	_push.lodCount = config.lodCount;
	_push.rootTiles = config.rootTilesX;
	_push.patchIndexCount = config.PatchIndexCount();

	// The pyramid HiZCull1 rebuilds just before this pass: previous frame's
	// resolved depth, terrain included.
	Handle<Texture> hiZTexture = frameResources.GetRenderTarget(HiZCullPass::RT_HIZ);
	bool hiZBound = builder.HasTexture(HiZCullPass::RT_HIZ) && hiZTexture.IsValid();
	_occlusionEnabled = hiZBound && frameResources.GetPreviousDepthBuffer().IsValid();

	TerrainCullData cullData{};
	cullData.view = camera->GetView();
	cullData.proj = camera->GetProjection();
	Math::ExtractFrustumPlanes(cullData.proj * cullData.view, cullData.frustumPlanes);
	cullData.screenHiZ = vec4(float(_screenExtent.width), float(_screenExtent.height),
		_occlusionEnabled ? float(hiZTexture.Get().GetImage().GetMipLevel()) : 1.0f,
		_occlusionEnabled ? 1.0f : 0.0f);
	// Same conservative pad the CPU culler uses for decimated coarse bakes.
	cullData.heightBounds = vec4(config.heightMin, config.heightMax, 2.0f, 0.0f);

	auto cullDataHandle = frameResources.GetOrCreateUniformBuffer<TerrainCullData>("Terrain.CullData");
	cullDataHandle.Get().Update(cullData);
	_cullData = builder.ImportBuffer("Terrain.CullData", cullDataHandle);
	builder.Read(_cullData, BufferAccess::UniformCompute);

	_nodeList = builder.GetBuffer(TerrainNodeListPass::SB_NODE_LIST);
	builder.Read(_nodeList, BufferAccess::StorageComputeRead);

	_nodeListCount = builder.GetBuffer(TerrainNodeListPass::SB_NODE_LIST_COUNT);
	builder.Read(_nodeListCount, BufferAccess::IndirectRead);

	_nodeDescs = builder.ImportBuffer(TerrainQuadTree::NODE_DESC,
		_terrain.GetQuadTree().GetNodeDescBuffer());
	builder.Read(_nodeDescs, BufferAccess::StorageComputeRead);

	_lodMap = builder.GetTexture(TerrainLodMapPass::RT_LOD_MAP);
	builder.Read(_lodMap, TextureAccess::SampledCompute);

	if (hiZBound)
	{
		_hiZ = builder.GetTexture(HiZCullPass::RT_HIZ);
	}
	else
	{
		// No pyramid this frame
		_hiZ = builder.ImportTexture(TerrainQuadTree::HEIGHT_ATLAS,
			_terrain.GetQuadTree().GetHeightAtlas());
	}
	builder.Read(_hiZ, TextureAccess::SampledCompute);

	_patchList = builder.CreateBuffer(SB_PATCH_LIST,
		{ config.MaxPatches() * sizeof(uvec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
	builder.Write(_patchList, BufferAccess::StorageComputeWrite);

	_patchDrawArgs = builder.GetBuffer(TerrainNodeListPass::SB_PATCH_DRAW_ARGS);
	builder.Write(_patchDrawArgs, BufferAccess::StorageComputeWrite);

	// Stats feed: TerrainSystem owns the slots and reads them in OnGUI.
	uint32_t slot = uint32_t(FrameCounter::GetFrameNumber() % MAX_FRAMES_IN_FLIGHT);
	_readback = builder.ImportBuffer("Terrain.PatchCountReadback" + to_string(slot),
		_terrain.GetPatchCountReadback(slot));
	builder.Write(_readback, BufferAccess::TransferDst);

	_active = true;
}

void TerrainPatchCullPass::Execute(FrameGraphPassContext& context,
	CommandBuffer& commandBuffer)
{
	if (!_active)
		return;

	Shader& shader = _shader.Get();
	commandBuffer.BindPipeline(&_pipeline.Get());

	auto builder = context.CreateDescriptorSetBuilder(shader, 0);
	builder.SetStorageBuffer(0, context.GetBuffer(_nodeList));
	builder.SetStorageBuffer(1, context.GetBuffer(_nodeDescs));
	builder.SetTextureBuffer(2, context.GetTexture(_lodMap));
	builder.SetTextureBuffer(3, context.GetTexture(_hiZ));
	builder.SetStorageBuffer(4, context.GetBuffer(_patchList));
	builder.SetStorageBuffer(5, context.GetBuffer(_patchDrawArgs));
	builder.SetUniformBuffer(6, context.GetBuffer(_cullData));
	
	auto& resources = builder.Build();

	commandBuffer.BindDescriptorSet(_pipeline.Get().GetPipelineBindPoint(),
		shader, resources);

	commandBuffer.PushConstants(shader, 0, _push);

	// One workgroup per listed node, straight from the node list's args.
	commandBuffer.DispatchIndirect(context.GetBuffer(_nodeListCount),
		sizeof(uint32_t));

	// Stats readback of instanceCount (offset 4 in the draw args).
	Buffer& drawArgs = context.GetBuffer(_patchDrawArgs);
	commandBuffer.CreateBarrierBatch()
		.Buffer(drawArgs,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT)
		.Submit();

	commandBuffer.CopyBuffer(drawArgs, context.GetBuffer(_readback),
		0, sizeof(uint32_t), sizeof(uint32_t));
}
