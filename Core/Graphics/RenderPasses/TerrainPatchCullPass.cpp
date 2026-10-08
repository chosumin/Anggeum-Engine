#include "stdafx.h"
#include "TerrainPatchCullPass.h"
#include "TerrainNodeListPass.h"
#include "HiZCullPass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/FrameResources.h"
#include "Graphics/RenderScene.h"
#include "Graphics/FrameCounter.h"
#include "Graphics/Terrain/TerrainSystem.h"
#include "Foundation/Scene.h"
#include "Components/PerspectiveCamera.h"
#include "Utils/Math.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Image.h"
#include "Graphics/Vulkans/Buffer.h"

using namespace Core;

TerrainPatchCullPass::TerrainPatchCullPass(RenderScene& renderScene, TerrainPatchCuller& culler,
	VkExtent2D screenExtent)
	: _renderScene(renderScene)
	, _terrain(renderScene.GetTerrainSystem())
	, _culler(culler)
	, _screenExtent(screenExtent)
{
}

void TerrainPatchCullPass::Setup(FrameGraphBuilder& builder,
	FrameResources& frameResources, RenderFrame& renderFrame)
{
	_active = false;

	PerspectiveCamera* camera = _renderScene.GetScene().GetMainCamera();
	if (!camera || !builder.HasBuffer(TerrainNodeListPass::SB_NODE_LIST))
		return;

	const TerrainConfig& config = _terrain.GetConfig();
	_push = _culler.BuildPush(*camera);

	// The pyramid HiZCull1 rebuilds just before this pass: previous frame's
	// resolved depth, terrain included.
	Handle<Texture> hiZTexture = frameResources.GetRenderTarget(HiZCullPass::RT_HIZ);
	_hiZBound = builder.HasTexture(HiZCullPass::RT_HIZ) && hiZTexture.IsValid();
	const bool occlusionEnabled = _hiZBound && frameResources.GetPreviousDepthBuffer().IsValid();

	TerrainCullData cullData{};
	cullData.view = camera->GetView();
	cullData.proj = camera->GetProjection();
	Math::ExtractFrustumPlanes(cullData.proj * cullData.view, cullData.frustumPlanes);
	cullData.screenHiZ = vec4(float(_screenExtent.width), float(_screenExtent.height),
		occlusionEnabled ? float(hiZTexture.Get().GetImage().GetMipLevel()) : 1.0f,
		occlusionEnabled ? 1.0f : 0.0f);
	// Same conservative pad the CPU culler uses for decimated coarse bakes.
	cullData.heightBounds = vec4(config.heightMin, config.heightMax, 2.0f, 0.0f);
	cullData.debug = uvec4(_terrain.IsCullingBypassed() ? 1u : 0u, 0u, 0u, 0u);

	auto cullDataHandle = frameResources.GetOrCreateUniformBuffer<TerrainCullData>("Terrain.CullData");
	cullDataHandle.Get().Update(cullData);
	_cullData = builder.ImportBuffer("Terrain.CullData", cullDataHandle);
	builder.Read(_cullData, BufferAccess::UniformCompute);

	_inputs = _culler.SetupInputs(builder, frameResources);

	if (_hiZBound)
	{
		_hiZ = builder.GetTexture(HiZCullPass::RT_HIZ);
		builder.Read(_hiZ, TextureAccess::SampledCompute);
	}

	_output.patchList = _culler.CreatePatchList(builder, SB_PATCH_LIST);
	_output.drawArgs = builder.GetBuffer(TerrainNodeListPass::SB_PATCH_DRAW_ARGS);
	builder.Write(_output.drawArgs, BufferAccess::StorageComputeWrite);

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

	_culler.Dispatch(context, commandBuffer, _inputs, _output, context.GetBuffer(_cullData),
		_hiZBound ? &context.GetTexture(_hiZ) : nullptr, _push);

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
