#include "stdafx.h"
#include "TerrainPatchCuller.h"
#include "TerrainSystem.h"
#include "Graphics/RenderPasses/TerrainNodeListPass.h"
#include "Graphics/RenderPasses/TerrainLodMapPass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/FrameResources.h"
#include "Graphics/ResourceManager.h"
#include "Components/PerspectiveCamera.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Graphics/Vulkans/Buffer.h"

using namespace Core;

TerrainPatchCuller::TerrainPatchCuller(ResourceManager& resourceManager, TerrainSystem& terrain)
	: _terrain(terrain)
{
	_shader = resourceManager.LoadShader("Shaders/Terrain/terrainPatchCull.comp.spv");
	_pipeline = resourceManager.LoadComputePipeline("Shaders/Terrain/terrainPatchCull.comp.spv");

	assert(_terrain.GetConfig().patchesPerNodeEdge == 8
		&& "terrainPatchCull.comp PATCHES_PER_EDGE / local_size mirror this");
}

TerrainPatchCuller::~TerrainPatchCuller() = default;

TerrainPatchCuller::Inputs TerrainPatchCuller::SetupInputs(FrameGraphBuilder& builder,
	FrameResources& frameResources)
{
	Inputs inputs;
	inputs.nodeList = builder.GetBuffer(TerrainNodeListPass::SB_NODE_LIST);
	builder.Read(inputs.nodeList, BufferAccess::StorageComputeRead);

	inputs.nodeListCount = builder.GetBuffer(TerrainNodeListPass::SB_NODE_LIST_COUNT);
	builder.Read(inputs.nodeListCount, BufferAccess::IndirectRead);

	inputs.lodMap = builder.GetTexture(TerrainLodMapPass::RT_LOD_MAP);
	builder.Read(inputs.lodMap, TextureAccess::SampledCompute);

	_nodeDescBuffer = frameResources.GetStorageBuffer(TerrainQuadTree::NODE_DESC);
	return inputs;
}

FGBuffer TerrainPatchCuller::CreatePatchList(FrameGraphBuilder& builder, const string& name)
{
	FGBuffer patchList = builder.CreateBuffer(name,
		{ _terrain.GetConfig().MaxPatches() * sizeof(uvec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
	builder.Write(patchList, BufferAccess::StorageComputeWrite);
	return patchList;
}

FGBuffer TerrainPatchCuller::CreateDrawArgs(FrameGraphBuilder& builder, const string& name)
{
	FGBuffer drawArgs = builder.CreateBuffer(name,
		{ 5 * sizeof(uint32_t),
		  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
		  | VK_BUFFER_USAGE_TRANSFER_DST_BIT });
	builder.Write(drawArgs, BufferAccess::FillComputeWrite);
	return drawArgs;
}

TerrainTraversalPush TerrainPatchCuller::BuildPush(const PerspectiveCamera& camera) const
{
	const TerrainConfig& config = _terrain.GetConfig();

	TerrainTraversalPush push{};
	push.cameraXZ = vec2(camera.Matrices.Position.x, camera.Matrices.Position.z);
	push.worldOrigin = config.WorldOrigin();
	push.rootNodeSize = config.rootNodeSize;
	push.ringRadiusScale = config.ringRadiusScale;
	push.lodCount = config.lodCount;
	push.rootTiles = config.rootTilesX;
	push.patchIndexCount = config.PatchIndexCount();
	return push;
}

void TerrainPatchCuller::ResetDrawArgs(CommandBuffer& commandBuffer, Buffer& drawArgs) const
{
	commandBuffer.FillBuffer(drawArgs, 0, sizeof(uint32_t), _terrain.GetConfig().PatchIndexCount());
	commandBuffer.FillBuffer(drawArgs, sizeof(uint32_t), 4 * sizeof(uint32_t), 0);
}

void TerrainPatchCuller::Dispatch(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
	const Inputs& inputs, const Output& output, Buffer& cullData,
	Texture* hiZ, const TerrainTraversalPush& push)
{
	Shader& shader = _shader.Get();
	commandBuffer.BindPipeline(&_pipeline.Get());

	auto builder = context.CreateDescriptorSetBuilder(shader, 0);
	builder.SetStorageBuffer(0, context.GetBuffer(inputs.nodeList));
	builder.SetStorageBuffer(1, _nodeDescBuffer.Get());
	builder.SetTextureBuffer(2, context.GetTexture(inputs.lodMap));
	// Occlusion off: the binding still needs a resident texture, so the height
	// atlas (GENERAL layout) stands in; the shader never samples it.
	if (hiZ != nullptr)
		builder.SetTextureBuffer(3, *hiZ);
	else
		builder.SetTextureBuffer(3, _terrain.GetQuadTree().GetHeightAtlas().Get(),
			0, VK_IMAGE_LAYOUT_GENERAL);
	builder.SetStorageBuffer(4, context.GetBuffer(output.patchList));
	builder.SetStorageBuffer(5, context.GetBuffer(output.drawArgs));
	builder.SetUniformBuffer(6, cullData);
	auto& resources = builder.Build();

	commandBuffer.BindDescriptorSet(_pipeline.Get().GetPipelineBindPoint(), shader, resources);
	commandBuffer.PushConstants(shader, 0, push);

	// One workgroup per listed node, straight from the node list's args.
	commandBuffer.DispatchIndirect(context.GetBuffer(inputs.nodeListCount), sizeof(uint32_t));
}
