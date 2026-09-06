#include "stdafx.h"
#include "TerrainLodMapPass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/FrameResources.h"
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
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Buffer.h"

using namespace Core;

TerrainLodMapPass::TerrainLodMapPass(Device& device, ResourceManager& resourceManager, RenderScene& renderScene)
	: _renderScene(renderScene)
	, _terrain(renderScene.GetTerrainSystem())
{
	
	_shader = resourceManager.LoadShader("Shaders/Terrain/terrainLodMap.comp.spv");
	_pipeline = resourceManager.LoadComputePipeline("Shaders/Terrain/terrainLodMap.comp.spv");

	_nearestSampler = resourceManager.LoadSampler(
		{ VK_FILTER_NEAREST, VK_FILTER_NEAREST,
		  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		  VK_SAMPLER_MIPMAP_MODE_NEAREST });

	_sectorsPerSide = _terrain.GetConfig().NodesPerSide(0);
}

TerrainLodMapPass::~TerrainLodMapPass() = default;

void TerrainLodMapPass::Setup(FrameGraphBuilder& builder,
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

	_indexTexture = frameResources.GetRenderTarget(TerrainQuadTree::QUADTREE_INDEX);

	// Pool-owned (not a graph transient) so it can carry a NEAREST sampler:
	// R8_UINT views reject the default LINEAR one. Content is still fully
	// rewritten every frame.
	RenderTargetDesc mapDesc{};
	mapDesc.extent = { _sectorsPerSide, _sectorsPerSide };
	mapDesc.format = VK_FORMAT_R8_UINT;
	mapDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
		| VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	mapDesc.sampler = _nearestSampler;
	_lodMap = builder.ImportTexture(RT_LOD_MAP,
		frameResources.GetOrCreateRenderTarget(RT_LOD_MAP, mapDesc));
	builder.Write(_lodMap, TextureAccess::StorageComputeWrite);

	_active = true;
}

void TerrainLodMapPass::Execute(FrameGraphPassContext& context,
	CommandBuffer& commandBuffer)
{
	if (!_active)
		return;

	Texture& lodMap = context.GetTexture(_lodMap);
	Shader& shader = _shader.Get();

	commandBuffer.BindPipeline(&_pipeline.Get());
	auto builder = context.CreateDescriptorSetBuilder(shader, 0);
	builder.SetTextureBuffer(0, _indexTexture.Get());
	builder.SetTextureBuffer(1, lodMap, 0, VK_IMAGE_LAYOUT_GENERAL);
	auto& resources = builder.Build();
	commandBuffer.BindDescriptorSet(_pipeline.Get().GetPipelineBindPoint(),
		shader, resources);

	commandBuffer.PushConstants(shader, 0, _push);

	uint32_t groups = (_sectorsPerSide + 7) / 8;
	commandBuffer.Dispatch(groups, groups, 1);
}
