#include "stdafx.h"
#include "TerrainDepthPrePass.h"
#include "DepthPrePass.h"
#include "TerrainNodeListPass.h"
#include "TerrainPatchCullPass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/FrameResources.h"
#include "Graphics/RenderScene.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/Terrain/TerrainSystem.h"
#include "Foundation/Scene.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/PipelineState.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"

using namespace Core;

TerrainDepthPrePass::TerrainDepthPrePass(Device& device, RenderScene& renderScene,
	VkFormat depthFormat, VkSampleCountFlagBits msaaSamples)
	: _renderScene(renderScene)
	, _terrain(renderScene.GetTerrainSystem())
{
	// terrain.vert + a normal-only fragment; positions are invariant with the
	// color pipeline's, so the color pass can rely on this depth exactly.
	_shader = device.GetResourceManager().LoadShader("TerrainDepth");

	_pipelineState = make_unique<PipelineState>();
	_pipelineState->GetMultisampleStateCreateInfo().rasterizationSamples = msaaSamples;
	_pipelineState->GetRasterizationStateCreateInfo().cullMode = VK_CULL_MODE_NONE;

	PipelineRenderingDesc renderingDesc;
	renderingDesc.colorFormats = { VK_FORMAT_R8G8B8A8_UNORM }; // MainNormal
	renderingDesc.depthFormat = depthFormat;
	_pipeline = make_unique<Pipeline>(device, renderingDesc, _shader.Get(), *_pipelineState);
}

TerrainDepthPrePass::~TerrainDepthPrePass() = default;

void TerrainDepthPrePass::Setup(FrameGraphBuilder& builder,
	FrameResources& frameResources, RenderFrame& renderFrame)
{
	_active = false;

	// The patch list comes from the cull pass earlier in the prepass phase;
	// the targets come from DepthPre1 (which always declares and clears them).
	if (!builder.HasBuffer(TerrainPatchCullPass::SB_PATCH_LIST)
		|| !builder.HasTexture(DepthPrePass::RT_MAIN_DEPTH))
		return;

	_mainNormal = builder.GetTexture(DepthPrePass::RT_MAIN_NORMAL);
	_mainDepth = builder.GetTexture(DepthPrePass::RT_MAIN_DEPTH);

	FGAttachment color;
	color.texture = _mainNormal;
	color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	builder.SetColorAttachment(0, color);

	FGAttachment depth;
	depth.texture = _mainDepth;
	depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	builder.SetDepthAttachment(depth);

	TerrainQuadTree& quadTree = _terrain.GetQuadTree();
	_height = builder.ImportTexture(TerrainQuadTree::HEIGHT_ATLAS, quadTree.GetHeightAtlas());
	builder.Read(_height, TextureAccess::SampledVertex);
	_normal = builder.ImportTexture(TerrainQuadTree::NORMAL_ATLAS, quadTree.GetNormalAtlas());
	builder.Read(_normal, TextureAccess::SampledFragment);

	_camera = builder.ImportBuffer(UB_CAMERA,
		frameResources.GetOrCreateUniformBuffer<CameraBuffer>(UB_CAMERA));
	builder.Read(_camera, BufferAccess::UniformVertex);

	_patchList = builder.GetBuffer(TerrainPatchCullPass::SB_PATCH_LIST);
	builder.Read(_patchList, BufferAccess::StorageVertexRead);

	_patchDrawArgs = builder.GetBuffer(TerrainNodeListPass::SB_PATCH_DRAW_ARGS);
	builder.Read(_patchDrawArgs, BufferAccess::IndirectRead);

	// Shared with TerrainPass: same builder, same buffer, refreshed by
	// whichever of the two passes sets up first.
	TerrainParams params = _terrain.BuildRenderParams(_renderScene.GetScene().GetMainLight());
	auto paramsHandle = frameResources.GetOrCreateUniformBuffer<TerrainParams>("Terrain.Params");
	paramsHandle.Get().Update(params);
	_params = builder.ImportBuffer("Terrain.Params", paramsHandle);
	builder.Read(_params, BufferAccess::UniformFragment);

	_active = true;
}

void TerrainDepthPrePass::Execute(FrameGraphPassContext& context,
	CommandBuffer& commandBuffer)
{
	if (!_active)
		return;

	context.BeginRendering(commandBuffer);
	commandBuffer.SetViewportAndScissor(context.GetRenderArea());

	commandBuffer.BindPipeline(_pipeline.get());

	Shader& shader = _shader.Get();
	auto builder = context.CreateDescriptorSetBuilder(shader, 0);
	builder.SetUniformBuffer(0, context.GetBuffer(_camera));
	builder.SetStorageBuffer(1, context.GetBuffer(_patchList));
	builder.SetTextureBuffer(2, context.GetTexture(_height));
	builder.SetTextureBuffer(3, context.GetTexture(_normal));
	builder.SetUniformBuffer(5, context.GetBuffer(_params));
	auto& resources = builder.Build();

	commandBuffer.BindDescriptorSet(_pipeline->GetPipelineBindPoint(), shader, resources);

	commandBuffer.BindIndexBuffer(_terrain.GetGridIndexBuffer().Get(), VK_INDEX_TYPE_UINT16);
	commandBuffer.DrawIndexedIndirect(context.GetBuffer(_patchDrawArgs), 1,
		sizeof(VkDrawIndexedIndirectCommand));

	context.EndRendering(commandBuffer);
}
