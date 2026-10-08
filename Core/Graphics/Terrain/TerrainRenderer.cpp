#include "stdafx.h"
#include "TerrainRenderer.h"
#include "TerrainSystem.h"
#include "Graphics/RenderPasses/TerrainNodeListPass.h"
#include "Graphics/RenderPasses/TerrainPatchCullPass.h"
#include "Graphics/RenderPasses/ShadowCullPass.h"
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
	RenderScene& renderScene, VkFormat colorFormat, VkFormat depthFormat,
	VkSampleCountFlagBits msaaSamples)
	: _renderScene(renderScene)
	, _terrain(renderScene.GetTerrainSystem())
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

bool TerrainRenderer::SetupShared(FrameGraphBuilder& builder, FrameResources& frameResources)
{
	if (!builder.HasBuffer(TerrainPatchCullPass::SB_PATCH_LIST))
		return false;

	_camera = builder.ImportBuffer(UB_CAMERA,
		frameResources.GetOrCreateUniformBuffer<CameraBuffer>(UB_CAMERA));
	builder.Read(_camera, BufferAccess::UniformVertex);

	_patchList = builder.GetBuffer(TerrainPatchCullPass::SB_PATCH_LIST);
	builder.Read(_patchList, BufferAccess::StorageVertexRead);

	_patchDrawArgs = builder.GetBuffer(TerrainNodeListPass::SB_PATCH_DRAW_ARGS);
	builder.Read(_patchDrawArgs, BufferAccess::IndirectRead);

	_params = SetupParams(builder, frameResources);
	return true;
}

FGBuffer TerrainRenderer::SetupParams(FrameGraphBuilder& builder, FrameResources& frameResources)
{
	// One buffer for every terrain draw this frame; refreshed by whichever
	// pass sets up first.
	TerrainParams params = _terrain.BuildRenderParams(_renderScene.GetScene().GetMainLight());
	auto paramsHandle = frameResources.GetOrCreateUniformBuffer<TerrainParams>("Terrain.Params");
	paramsHandle.Get().Update(params);
	FGBuffer fgParams = builder.ImportBuffer("Terrain.Params", paramsHandle);
	builder.Read(fgParams, BufferAccess::UniformFragment);
	return fgParams;
}

void TerrainRenderer::BindShared(FrameGraphPassContext& context, DescriptorSetBuilder& builder)
{
	// The atlases are not graph resources: they live in GENERAL layout forever
	// (the transfer queue streams tiles into them), read-only in here.
	TerrainQuadTree& quadTree = _terrain.GetQuadTree();

	builder.SetUniformBuffer(0, context.GetBuffer(_camera));
	builder.SetStorageBuffer(1, context.GetBuffer(_patchList));
	builder.SetTextureBuffer(2, quadTree.GetHeightAtlas().Get(), 0, VK_IMAGE_LAYOUT_GENERAL);
	builder.SetTextureBuffer(3, quadTree.GetNormalAtlas().Get(), 0, VK_IMAGE_LAYOUT_GENERAL);
	builder.SetUniformBuffer(5, context.GetBuffer(_params));
}

bool TerrainRenderer::SetupDepth(FrameGraphBuilder& builder, FrameResources& frameResources)
{
	_depthActive = SetupShared(builder, frameResources);
	return _depthActive;
}

void TerrainRenderer::RecordDepth(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
	if (!_depthActive)
		return;

	commandBuffer.BindPipeline(_depthPipeline.get());

	Shader& shader = _depthShader.Get();
	auto builder = context.CreateDescriptorSetBuilder(shader, 0);
	BindShared(context, builder);
	auto& resources = builder.Build();

	commandBuffer.BindDescriptorSet(_depthPipeline->GetPipelineBindPoint(), shader, resources);

	commandBuffer.BindIndexBuffer(_terrain.GetGridIndexBuffer().Get(), VK_INDEX_TYPE_UINT16);
	commandBuffer.DrawIndexedIndirect(context.GetBuffer(_patchDrawArgs), 1,
		sizeof(VkDrawIndexedIndirectCommand));
}

bool TerrainRenderer::SetupColor(FrameGraphBuilder& builder, FrameResources& frameResources)
{
	_colorActive = SetupShared(builder, frameResources);
	if (!_colorActive)
		return false;

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
	auto builder = context.CreateDescriptorSetBuilder(shader, 0);
	BindShared(context, builder);
	builder.SetTextureBuffer(4, _terrain.GetQuadTree().GetAlbedoAtlas().Get(), 0,
		VK_IMAGE_LAYOUT_GENERAL);
	// Pick off: the binding still needs a buffer, and the shader never writes it.
	builder.SetStorageBuffer(6, _pick.IsValid()
		? context.GetBuffer(_pick) : context.GetBuffer(_patchList));
	auto& resources = builder.Build();

	commandBuffer.BindDescriptorSet(pipeline->GetPipelineBindPoint(), shader, resources);

	commandBuffer.BindIndexBuffer(_terrain.GetGridIndexBuffer().Get(), VK_INDEX_TYPE_UINT16);
	commandBuffer.DrawIndexedIndirect(context.GetBuffer(_patchDrawArgs), 1,
		sizeof(VkDrawIndexedIndirectCommand));
}
