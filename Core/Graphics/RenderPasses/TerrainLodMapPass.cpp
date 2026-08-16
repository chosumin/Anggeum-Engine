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

TerrainLodMapPass::TerrainLodMapPass(Device& device, RenderScene& renderScene)
	: _renderScene(renderScene)
	, _terrain(renderScene.GetTerrainSystem())
{
	auto& resourceManager = device.GetResourceManager();
	_shader = resourceManager.LoadShader("Shaders/Terrain/terrainLodMap.comp.spv");
	_pipeline = resourceManager.LoadComputePipeline("Shaders/Terrain/terrainLodMap.comp.spv");

	_nearestSampler = resourceManager.LoadSampler(
		{ VK_FILTER_NEAREST, VK_FILTER_NEAREST,
		  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		  VK_SAMPLER_MIPMAP_MODE_NEAREST });

	_sectorsPerSide = _terrain.GetConfig().NodesPerSide(0);

	for (uint32_t slot = 0; slot < MAX_FRAMES_IN_FLIGHT; ++slot)
	{
		_mapReadback[slot] = resourceManager.LoadBuffer(
			{ VkDeviceSize(_sectorsPerSide) * _sectorsPerSide,
			  VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryType::UNIFORM },
			"Terrain.LodMapReadback" + to_string(slot));

		// Fresh device memory is undefined; zero so the first two frames'
		// validation reads compare against a defined state.
		void* mapped = nullptr;
		_mapReadback[slot].Get().GetMappedPtr(&mapped);
		memset(mapped, 0, size_t(_sectorsPerSide) * _sectorsPerSide);
	}
}

TerrainLodMapPass::~TerrainLodMapPass() = default;

void TerrainLodMapPass::Setup(FrameGraphBuilder& builder,
	FrameResources& frameResources, RenderFrame& renderFrame)
{
	_active = false;

	PerspectiveCamera* camera = _renderScene.GetScene().GetMainCamera();
	if (!camera)
		return;

	uint32_t slot = uint32_t(FrameCounter::GetFrameNumber() % MAX_FRAMES_IN_FLIGHT);

	// Bring-up validation: compare the 2-frame-old GPU map with the CPU
	// covering-set expectation (fence-safe, same scheme as the node count).
	void* mapped = nullptr;
	_mapReadback[slot].Get().GetMappedPtr(&mapped);
	_terrain.ValidateGpuLodMap(static_cast<const uint8_t*>(mapped));

	const TerrainConfig& config = _terrain.GetConfig();
	_push.cameraXZ = vec2(camera->Matrices.Position.x, camera->Matrices.Position.z);
	_push.worldOrigin = config.WorldOrigin();
	_push.rootNodeSize = config.rootNodeSize;
	_push.ringRadiusScale = config.ringRadiusScale;
	_push.lodCount = config.lodCount;
	_push.rootTiles = config.rootTilesX;

	_indexTexture = builder.ImportTexture(TerrainQuadTree::QUADTREE_INDEX,
		_terrain.GetQuadTree().GetIndexTexture());
	builder.Read(_indexTexture, TextureAccess::SampledCompute);

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

	_readback = builder.ImportBuffer("Terrain.LodMapReadback" + to_string(slot),
		_mapReadback[slot]);
	builder.Write(_readback, BufferAccess::TransferDst);

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
	builder.SetTextureBuffer(0, context.GetTexture(_indexTexture));
	builder.SetTextureBuffer(1, lodMap, 0, VK_IMAGE_LAYOUT_GENERAL);
	auto& resources = builder.Build();
	commandBuffer.BindDescriptorSet(_pipeline.Get().GetPipelineBindPoint(),
		shader, resources);

	commandBuffer.PushConstants(shader, 0, _push);

	uint32_t groups = (_sectorsPerSide + 7) / 8;
	commandBuffer.Dispatch(groups, groups, 1);

	// Bring-up readback: map -> host buffer for next-next frame's validation.
	commandBuffer.CreateBarrierBatch()
		.Image(lodMap, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT)
		.Submit();

	commandBuffer.CopyImageToBuffer(lodMap, VK_IMAGE_LAYOUT_GENERAL,
		context.GetBuffer(_readback), _sectorsPerSide, _sectorsPerSide);
}
