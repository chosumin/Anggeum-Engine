#include "stdafx.h"
#include "TerrainNodeListPass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/RenderScene.h"
#include "Graphics/RenderContext.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/Terrain/TerrainSystem.h"
#include "Foundation/Scene.h"
#include "Components/PerspectiveCamera.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Graphics/Vulkans/Buffer.h"

using namespace Core;

TerrainNodeListPass::~TerrainNodeListPass() = default;

TerrainNodeListPass::TerrainNodeListPass(Device& device, RenderScene& renderScene)
	: _renderScene(renderScene)
	, _terrain(renderScene.GetTerrainSystem())
{
	auto& resourceManager = device.GetResourceManager();
	_shader = resourceManager.LoadShader("Shaders/Terrain/terrainNodeList.comp.spv");

	const TerrainConfig& config = _terrain.GetConfig();
	assert(config.rootTilesX == config.rootTilesZ
		&& "square root-tile grids only (NodesPerSide assumes it too)");
	// The always-resident coarsest LOD means the roots occupy slots anyway,
	// and the shader's first level list holds them before filtering.
	assert(config.rootTilesX * config.rootTilesZ <= config.atlasCapacity);

	// constant_id 0 = MAX_NODES. Every listed node is resident, so the atlas
	// capacity bounds the list; it sizes the shader's shared-memory level
	// lists (2 x 4 bytes per node - mind the device's shared memory budget
	// when growing the capacity).
	_pipeline = make_unique<Pipeline>(device, _shader.Get(),
		SpecConstants{ { { 0, config.atlasCapacity } } });
}

void TerrainNodeListPass::Setup(FrameGraphBuilder& builder,
	FrameResources& frameResources, RenderFrame& renderFrame)
{
	_active = false;

	PerspectiveCamera* camera = _renderScene.GetScene().GetMainCamera();
	if (!camera)
		return;

	const TerrainConfig& config = _terrain.GetConfig();
	_push.cameraXZ = vec2(camera->Matrices.Position.x, camera->Matrices.Position.z);
	_push.worldOrigin = config.WorldOrigin();
	_push.rootNodeSize = config.rootNodeSize;
	_push.ringRadiusScale = config.ringRadiusScale;
	_push.lodCount = config.lodCount;
	_push.rootTiles = config.rootTilesX;
	_push.patchIndexCount = config.PatchIndexCount();

	_indexTexture = builder.ImportTexture(TerrainQuadTree::QUADTREE_INDEX,
		_terrain.GetQuadTree().GetIndexTexture());
	builder.Read(_indexTexture, TextureAccess::SampledCompute);

	_nodeList = builder.CreateBuffer(SB_NODE_LIST,
		{ config.atlasCapacity * sizeof(uvec2), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
	builder.Write(_nodeList, BufferAccess::StorageComputeWrite);

	// {count, groupsX, groupsY, groupsZ}: the tail three double as the
	// VkDispatchIndirectCommand for one-group-per-node consumers (offset 4).
	_nodeListCount = builder.CreateBuffer(SB_NODE_LIST_COUNT,
		{ 4 * sizeof(uint32_t),
		  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
		  | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT });
	builder.Write(_nodeListCount, BufferAccess::StorageComputeWrite);

	_patchDrawArgs = builder.CreateBuffer(SB_PATCH_DRAW_ARGS,
		{ 5 * sizeof(uint32_t),
		  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
		  | VK_BUFFER_USAGE_TRANSFER_SRC_BIT });
	builder.Write(_patchDrawArgs, BufferAccess::StorageComputeWrite);

	_active = true;
}

void TerrainNodeListPass::Execute(FrameGraphPassContext& context,
	CommandBuffer& commandBuffer)
{
	if (!_active)
		return;

	Shader& shader = _shader.Get();
	commandBuffer.BindPipeline(_pipeline.get());

	auto builder = context.CreateDescriptorSetBuilder(shader, 0);
	builder.SetTextureBuffer(0, context.GetTexture(_indexTexture));
	builder.SetStorageBuffer(1, context.GetBuffer(_nodeList));
	builder.SetStorageBuffer(2, context.GetBuffer(_nodeListCount));
	builder.SetStorageBuffer(3, context.GetBuffer(_patchDrawArgs));
	auto& resources = builder.Build();
	commandBuffer.BindDescriptorSet(_pipeline->GetPipelineBindPoint(),
		shader, resources);

	commandBuffer.PushConstants(shader, 0, _push);

	// The whole traversal is one workgroup (see the shader's LDS ping-pong).
	commandBuffer.Dispatch(1, 1, 1);
}
