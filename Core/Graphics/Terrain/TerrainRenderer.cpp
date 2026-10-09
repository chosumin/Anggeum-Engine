#include "stdafx.h"
#include "TerrainRenderer.h"
#include "TerrainSystem.h"
#include "Graphics/RenderPasses/TerrainNodeListPass.h"
#include "Graphics/RenderPasses/HiZCullPass.h"
#include "Graphics/RenderPasses/ShadowCullPass.h"
#include "Graphics/RenderPasses/ShadowPass.h"
#include "Graphics/RenderPasses/SDFShadowPass.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/FrameResources.h"
#include "Graphics/FrameCounter.h"
#include "Graphics/RenderScene.h"
#include "Graphics/ResourceManager.h"
#include "Foundation/Scene.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/PipelineState.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"

using namespace Core;

TerrainRenderer::TerrainRenderer(Device& device, ResourceManager& resourceManager,
	RenderScene& renderScene, VkExtent2D screenExtent, VkFormat colorFormat,
	VkFormat depthFormat, VkSampleCountFlagBits msaaSamples)
	: _renderScene(renderScene)
	, _terrain(renderScene.GetTerrainSystem())
	, _screenExtent(screenExtent)
{
	// terrain.vert + a normal-only fragment; positions are invariant with the
	// color pipeline's, so the color draw can rely on this depth exactly.
	_depthShader = resourceManager.LoadShader("TerrainDepth");

	_depthPipelineState = make_unique<PipelineState>();
	_depthPipelineState->GetMultisampleStateCreateInfo().rasterizationSamples = msaaSamples;
	// Heightfields have no closed backside.
	_depthPipelineState->GetRasterizationStateCreateInfo().cullMode = VK_CULL_MODE_NONE;

	PipelineRenderingDesc depthDesc;
	depthDesc.colorFormats = { VK_FORMAT_R8G8B8A8_UNORM }; // MainNormal
	depthDesc.depthFormat = depthFormat;
	_depthPipeline = make_unique<Pipeline>(device, depthDesc, _depthShader.Get(),
		*_depthPipelineState);

	_colorShader = resourceManager.LoadShader("Terrain");

	_colorPipelineState = make_unique<PipelineState>(*_depthPipelineState);
	// The prepass owns the depth: LESS_OR_EQUAL against it, writes off.
	_colorPipelineState->GetDepthStencilStateCreateInfo().depthWriteEnable = VK_FALSE;

	PipelineRenderingDesc colorDesc;
	colorDesc.colorFormats = { colorFormat };
	colorDesc.depthFormat = depthFormat;
	_colorPipeline = make_unique<Pipeline>(device, colorDesc, _colorShader.Get(),
		*_colorPipelineState);

	auto wireframeState = *_colorPipelineState;
	wireframeState.GetRasterizationStateCreateInfo().polygonMode = VK_POLYGON_MODE_LINE;
	_wireframePipeline = make_unique<Pipeline>(device, colorDesc, _colorShader.Get(),
		wireframeState);

	// terrain.vert under the cascade camera + the empty shadow fragment.
	_shadowShader = resourceManager.LoadShader("TerrainShadow");

	_shadowPipelineState = make_unique<PipelineState>();
	_shadowPipelineState->GetRasterizationStateCreateInfo().cullMode = VK_CULL_MODE_NONE;
	_shadowPipelineState->GetRasterizationStateCreateInfo().depthBiasEnable = VK_TRUE;

	PipelineRenderingDesc shadowDesc;
	shadowDesc.depthFormat = depthFormat;
	_shadowPipeline = make_unique<Pipeline>(device, shadowDesc, _shadowShader.Get(),
		*_shadowPipelineState);
}

TerrainRenderer::~TerrainRenderer() = default;

TerrainRenderer::PatchList TerrainRenderer::SetupPatchList(FrameGraphBuilder& builder,
	const char* listName, const char* argsName)
{
	PatchList list;
	if (!builder.HasBuffer(listName))
		return list;

	list.patches = builder.GetBuffer(listName);
	builder.Read(list.patches, BufferAccess::StorageVertexRead);

	list.drawArgs = builder.GetBuffer(argsName);
	builder.Read(list.drawArgs, BufferAccess::IndirectRead);
	return list;
}

// Declares every list that exists this frame; false when there is none.
bool TerrainRenderer::SetupShared(FrameGraphBuilder& builder, FrameResources& frameResources)
{
	_lists[0] = SetupPatchList(builder, HiZCullPass::SB_TERRAIN_PATCH_LIST,
		TerrainNodeListPass::SB_PATCH_DRAW_ARGS);
	_lists[1] = SetupPatchList(builder, HiZCullPass::SB_TERRAIN_PASS2_PATCH_LIST,
		HiZCullPass::SB_TERRAIN_PASS2_DRAW_ARGS);
	if (!_lists[0].IsValid() && !_lists[1].IsValid())
		return false;

	_camera = builder.ImportBuffer(UB_CAMERA,
		frameResources.GetOrCreateUniformBuffer<CameraBuffer>(UB_CAMERA));
	builder.Read(_camera, BufferAccess::UniformVertex);

	_params = SetupParams(builder, frameResources);
	return true;
}

FGBuffer TerrainRenderer::SetupParams(FrameGraphBuilder& builder, FrameResources& frameResources)
{
	// One buffer for every terrain draw this frame; refreshed by whichever
	// pass sets up first.
	TerrainParams params = _terrain.BuildRenderParams(_renderScene.GetScene().GetMainLight());
	params.viewport = vec4(float(_screenExtent.width), float(_screenExtent.height), 0.0f, 0.0f);
	auto paramsHandle = frameResources.GetOrCreateUniformBuffer<TerrainParams>("Terrain.Params");
	paramsHandle.Get().Update(params);
	FGBuffer fgParams = builder.ImportBuffer("Terrain.Params", paramsHandle);
	builder.Read(fgParams, BufferAccess::UniformFragment);
	return fgParams;
}

void TerrainRenderer::BindShared(FrameGraphPassContext& context, DescriptorSetBuilder& builder,
	const PatchList& list)
{
	// The atlases are not graph resources: they live in GENERAL layout forever
	// (the transfer queue streams tiles into them), read-only in here.
	TerrainQuadTree& quadTree = _terrain.GetQuadTree();

	builder.SetUniformBuffer(0, context.GetBuffer(_camera));
	builder.SetStorageBuffer(1, context.GetBuffer(list.patches));
	builder.SetTextureBuffer(2, quadTree.GetHeightAtlas().Get(), 0, VK_IMAGE_LAYOUT_GENERAL);
	builder.SetTextureBuffer(3, quadTree.GetNormalAtlas().Get(), 0, VK_IMAGE_LAYOUT_GENERAL);
	builder.SetUniformBuffer(5, context.GetBuffer(_params));
}

void TerrainRenderer::DrawList(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
	const PatchList& list)
{
	commandBuffer.BindIndexBuffer(_terrain.GetGridIndexBuffer().Get(), VK_INDEX_TYPE_UINT16);
	commandBuffer.DrawIndexedIndirect(context.GetBuffer(list.drawArgs), 1,
		sizeof(VkDrawIndexedIndirectCommand));
}

bool TerrainRenderer::SetupDepth(FrameGraphBuilder& builder, FrameResources& frameResources,
	Phase phase)
{
	SetupShared(builder, frameResources);
	return _lists[int(phase)].IsValid();
}

void TerrainRenderer::RecordDepth(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
	Phase phase)
{
	const PatchList& list = _lists[int(phase)];
	if (!list.IsValid())
		return;

	commandBuffer.BindPipeline(_depthPipeline.get());

	Shader& shader = _depthShader.Get();
	auto builder = context.CreateDescriptorSetBuilder(shader, 0);
	BindShared(context, builder, list);
	auto& resources = builder.Build();

	commandBuffer.BindDescriptorSet(_depthPipeline->GetPipelineBindPoint(), shader, resources);
	DrawList(context, commandBuffer, list);
}

bool TerrainRenderer::SetupColor(FrameGraphBuilder& builder, FrameResources& frameResources)
{
	_colorActive = SetupShared(builder, frameResources);
	if (!_colorActive)
		return false;

	_sdfShadow = FGTexture{};
	if (builder.HasTexture(SDFShadowPass::RT_SDF_SHADOW))
	{
		_sdfShadow = builder.GetTexture(SDFShadowPass::RT_SDF_SHADOW);
		builder.Read(_sdfShadow, TextureAccess::SampledFragment);
	}

	_shadowMap = builder.GetTexture(ShadowPass::RT_SHADOW_DEPTH);
	builder.Read(_shadowMap, TextureAccess::SampledFragment);

	_shadowUB = builder.ImportBuffer(UB_SHADOW,
		frameResources.GetOrCreateUniformBuffer<ShadowUniform>(UB_SHADOW));
	builder.Read(_shadowUB, BufferAccess::UniformFragment);

	// Debug pick: TerrainSystem owns the slots and reads them in OnGUI.
	_pick = FGBuffer{};
	uint32_t slot = uint32_t(FrameCounter::GetFrameNumber() % MAX_FRAMES_IN_FLIGHT);
	if (_terrain.GetPickReadback(slot).IsValid())
	{
		_pick = builder.ImportBuffer("Terrain.PickReadback" + to_string(slot),
			_terrain.GetPickReadback(slot));
		builder.Write(_pick, BufferAccess::StorageFragmentWrite);
	}

	return true;
}

void TerrainRenderer::SetupShadow(FrameGraphBuilder& builder, FrameResources& frameResources,
	uint32_t cascadeCount)
{
	_shadowPatchLists.fill(FGBuffer{});
	_shadowDrawArgs.fill(FGBuffer{});

	bool any = false;
	for (uint32_t i = 0; i < cascadeCount; ++i)
	{
		string listName = ShadowCullPass::TerrainPatchListName(i);
		if (!builder.HasBuffer(listName))
			continue;

		_shadowPatchLists[i] = builder.GetBuffer(listName);
		builder.Read(_shadowPatchLists[i], BufferAccess::StorageVertexRead);

		_shadowDrawArgs[i] = builder.GetBuffer(ShadowCullPass::TerrainDrawArgsName(i));
		builder.Read(_shadowDrawArgs[i], BufferAccess::IndirectRead);
		any = true;
	}

	if (any)
		_shadowParams = SetupParams(builder, frameResources);
}

void TerrainRenderer::RecordShadow(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
	uint32_t cascade, Buffer& cascadeCamera)
{
	if (!_shadowPatchLists[cascade].IsValid())
		return;

	commandBuffer.BindPipeline(_shadowPipeline.get());

	Shader& shader = _shadowShader.Get();
	auto builder = context.CreateDescriptorSetBuilder(shader, 0);
	builder.SetUniformBuffer(0, cascadeCamera);
	builder.SetStorageBuffer(1, context.GetBuffer(_shadowPatchLists[cascade]));
	builder.SetTextureBuffer(2, _terrain.GetQuadTree().GetHeightAtlas().Get(), 0,
		VK_IMAGE_LAYOUT_GENERAL);
	builder.SetUniformBuffer(5, context.GetBuffer(_shadowParams));

	auto& resources = builder.Build();

	commandBuffer.BindDescriptorSet(_shadowPipeline->GetPipelineBindPoint(), shader, resources);

	commandBuffer.BindIndexBuffer(_terrain.GetGridIndexBuffer().Get(), VK_INDEX_TYPE_UINT16);

	commandBuffer.DrawIndexedIndirect(context.GetBuffer(_shadowDrawArgs[cascade]), 1,
		sizeof(VkDrawIndexedIndirectCommand));
}

void TerrainRenderer::RecordColor(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
	if (!_colorActive)
		return;

	Pipeline* pipeline = _terrain.IsWireframe()
		? _wireframePipeline.get() : _colorPipeline.get();
	commandBuffer.BindPipeline(pipeline);

	Shader& shader = _colorShader.Get();
	for (const PatchList& list : _lists)
	{
		if (!list.IsValid())
			continue;

		auto builder = context.CreateDescriptorSetBuilder(shader, 0);
		BindShared(context, builder, list);
		builder.SetTextureBuffer(4, _terrain.GetQuadTree().GetAlbedoAtlas().Get(), 0,
			VK_IMAGE_LAYOUT_GENERAL);

		// Pick off: the binding still needs a buffer, and the shader never writes it.
		builder.SetStorageBuffer(6, _pick.IsValid()
			? context.GetBuffer(_pick) : context.GetBuffer(list.patches));

		builder.SetUniformBuffer(7, context.GetBuffer(_shadowUB));
		builder.SetTextureBuffer(8, context.GetTexture(_shadowMap));

		// No mask yet: any 2D texture fills the slot, the shader skips the sample.
		if (_sdfShadow.IsValid())
			builder.SetTextureBuffer(9, context.GetTexture(_sdfShadow));
		else
			builder.SetTextureBuffer(9, _terrain.GetQuadTree().GetAlbedoAtlas().Get(), 0,
				VK_IMAGE_LAYOUT_GENERAL);

		auto& resources = builder.Build();

		commandBuffer.BindDescriptorSet(pipeline->GetPipelineBindPoint(), shader, resources);

		DrawList(context, commandBuffer, list);
	}
}
