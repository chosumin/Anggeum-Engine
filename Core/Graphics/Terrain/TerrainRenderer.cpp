#include "stdafx.h"
#include "TerrainRenderer.h"
#include "TerrainSystem.h"
#include "Graphics/RenderPasses/TerrainNodeListPass.h"
#include "Graphics/RenderPasses/TerrainPatchCullPass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/FrameResources.h"
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
	RenderScene& renderScene, VkFormat depthFormat, VkSampleCountFlagBits msaaSamples)
	: _renderScene(renderScene)
	, _terrain(renderScene.GetTerrainSystem())
{
	// terrain.vert + a normal-only fragment; positions are invariant with the
	// color pipeline's, so the color pass can rely on this depth exactly.
	_depthShader = resourceManager.LoadShader("TerrainDepth");

	_depthPipelineState = make_unique<PipelineState>();
	_depthPipelineState->GetMultisampleStateCreateInfo().rasterizationSamples = msaaSamples;
	_depthPipelineState->GetRasterizationStateCreateInfo().cullMode = VK_CULL_MODE_NONE;

	PipelineRenderingDesc renderingDesc;
	renderingDesc.colorFormats = { VK_FORMAT_R8G8B8A8_UNORM }; // MainNormal
	renderingDesc.depthFormat = depthFormat;
	_depthPipeline = make_unique<Pipeline>(device, renderingDesc, _depthShader.Get(),
		*_depthPipelineState);
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

	// One buffer for every terrain draw this frame; refreshed by whichever
	// pass sets up first.
	TerrainParams params = _terrain.BuildRenderParams(_renderScene.GetScene().GetMainLight());
	auto paramsHandle = frameResources.GetOrCreateUniformBuffer<TerrainParams>("Terrain.Params");
	paramsHandle.Get().Update(params);
	_params = builder.ImportBuffer("Terrain.Params", paramsHandle);
	builder.Read(_params, BufferAccess::UniformFragment);

	return true;
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

	// The atlases are not graph resources: they live in GENERAL layout forever
	// (the transfer queue streams tiles into them), read-only in here.
	TerrainQuadTree& quadTree = _terrain.GetQuadTree();

	Shader& shader = _depthShader.Get();
	auto builder = context.CreateDescriptorSetBuilder(shader, 0);
	builder.SetUniformBuffer(0, context.GetBuffer(_camera));
	builder.SetStorageBuffer(1, context.GetBuffer(_patchList));
	builder.SetTextureBuffer(2, quadTree.GetHeightAtlas().Get(), 0, VK_IMAGE_LAYOUT_GENERAL);
	builder.SetTextureBuffer(3, quadTree.GetNormalAtlas().Get(), 0, VK_IMAGE_LAYOUT_GENERAL);
	builder.SetUniformBuffer(5, context.GetBuffer(_params));
	auto& resources = builder.Build();

	commandBuffer.BindDescriptorSet(_depthPipeline->GetPipelineBindPoint(), shader, resources);

	commandBuffer.BindIndexBuffer(_terrain.GetGridIndexBuffer().Get(), VK_INDEX_TYPE_UINT16);
	commandBuffer.DrawIndexedIndirect(context.GetBuffer(_patchDrawArgs), 1,
		sizeof(VkDrawIndexedIndirectCommand));
}
