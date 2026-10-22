#include "stdafx.h"
#include "TerrainPatchCullPass.h"
#include "TerrainNodeListPass.h"
#include "HiZPyramid.h"
#include "Graphics/Terrain/TerrainSystem.h"
#include "Graphics/FrameCounter.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/FrameResources.h"
#include "Graphics/RenderScene.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Texture.h"
#include "Utils/Math.h"
#include "Foundation/Scene.h"
#include "Components/PerspectiveCamera.h"

using namespace Core;

TerrainPatchCullPass::TerrainPatchCullPass(RenderScene& renderScene,
	TerrainPatchCuller& terrainCuller, VkExtent2D screenExtent)
	: _renderScene(renderScene)
	, _terrainCuller(terrainCuller)
	, _extent(screenExtent)
	, _hiZMipLevels(HiZPyramid::MipLevelsFor(screenExtent))
{
}

TerrainPatchCullPass::~TerrainPatchCullPass() = default;

void TerrainPatchCullPass::Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
	RenderFrame& renderFrame)
{
	_active = false;

	PerspectiveCamera* camera = _renderScene.GetScene().GetMainCamera();
	if (!camera || !builder.HasBuffer(TerrainNodeListPass::SB_NODE_LIST))
		return;

	_camera = camera->Matrices;
	_push = _terrainCuller.BuildPush(*camera);

	_inputs = _terrainCuller.SetupInputs(builder, frameResources);

	_output.patchList = _terrainCuller.CreatePatchList(builder, SB_PATCH_LIST);
	_output.drawArgs = builder.GetBuffer(TerrainNodeListPass::SB_PATCH_DRAW_ARGS);
	builder.Write(_output.drawArgs, BufferAccess::StorageComputeWrite);
	_output.rejectedList = _terrainCuller.CreateRejectedList(builder, SB_REJECTED);
	_output.rejectedCount = _terrainCuller.CreateRejectedCount(builder, SB_REJECTED_COUNT);

	// Stats feed: TerrainSystem owns the slots and reads them in OnGUI.
	uint32_t slot = uint32_t(FrameCounter::GetFrameNumber() % MAX_FRAMES_IN_FLIGHT);
	_readback = builder.ImportBuffer("Terrain.PatchCountReadback" + to_string(slot),
		_renderScene.GetTerrainSystem().GetPatchCountReadback(slot));
	builder.Write(_readback, BufferAccess::TransferDst);

	_cullData = frameResources.GetOrCreateUniformBuffer<TerrainCullData>("Terrain.CullData");

	// Stale after a resize recreates the targets: treat as not built.
	_prevHiZ = frameResources.GetPreviousHiZ();
	if (_prevHiZ.TryGet() == nullptr)
		_prevHiZ = Handle<Texture>{};
	if (_prevHiZ.IsValid())
	{
		FGTexture prevHiZ = builder.ImportTexture("OcclusionCull.PrevHiZ", _prevHiZ);
		builder.Read(prevHiZ, TextureAccess::SampledCompute);
	}

	_active = true;
}

void TerrainPatchCullPass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
	if (!_active)
		return;

	const TerrainSystem& terrain = _renderScene.GetTerrainSystem();
	const TerrainConfig& config = terrain.GetConfig();
	Texture* hiZ = _prevHiZ.TryGet();

	TerrainCullData cullData{};
	cullData.view = _camera.View;
	cullData.proj = _camera.Projection;
	Math::ExtractFrustumPlanes(_camera.Projection * _camera.View, cullData.frustumPlanes);
	cullData.screenHiZ = vec4(float(_extent.width), float(_extent.height),
		float(_hiZMipLevels), hiZ != nullptr ? 1.0f : 0.0f);
	// Same conservative pad the CPU culler uses for decimated coarse bakes.
	cullData.heightBounds = vec4(config.heightMin, config.heightMax, 2.0f, 0.0f);
	cullData.debug = uvec4(terrain.IsCullingBypassed() ? 1u : 0u, 0u, 0u, 0u);

	Buffer& cullDataBuffer = _cullData.Get();
	cullDataBuffer.Update(cullData);

	Buffer& rejectedCount = context.GetBuffer(_output.rejectedCount);
	commandBuffer.FillBuffer(rejectedCount, 0, VK_WHOLE_SIZE, 0);
	commandBuffer.CreateBarrierBatch()
		.Buffer(rejectedCount,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)
		.Submit();

	_terrainCuller.Dispatch(context, commandBuffer, _inputs, _output,
		cullDataBuffer, hiZ, _push);

	// Stats readback of instanceCount (offset 4 in the draw args).
	Buffer& drawArgs = context.GetBuffer(_output.drawArgs);
	commandBuffer.CreateBarrierBatch()
		.Buffer(drawArgs,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT)
		.Submit();
	commandBuffer.CopyBuffer(drawArgs, context.GetBuffer(_readback),
		0, sizeof(uint32_t), sizeof(uint32_t));
}
