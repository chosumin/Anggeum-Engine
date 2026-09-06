#include "stdafx.h"
#include "TerrainPass.h"
#include "GeometryPass.h"
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

TerrainPass::TerrainPass(Device& device, ResourceManager& resourceManager, RenderScene& renderScene,
	VkFormat colorFormat, VkFormat depthFormat, VkSampleCountFlagBits msaaSamples)
	: _device(device)
	, _renderScene(renderScene)
	, _terrain(renderScene.GetTerrainSystem())
{
	_shader = resourceManager.LoadShader("Terrain");

	_pipelineState = make_unique<PipelineState>();
	_pipelineState->GetMultisampleStateCreateInfo().rasterizationSamples = msaaSamples;
	// Heightfields have no closed backside; GPU patch cone culling replaces
	// this in the phase-2 pipeline.
	_pipelineState->GetRasterizationStateCreateInfo().cullMode = VK_CULL_MODE_NONE;
	// TerrainDepthPrePass owns the depth; this pass draws early-z against it
	// (LESS_OR_EQUAL default + invariant positions), like GeometryPass does.
	_pipelineState->GetDepthStencilStateCreateInfo().depthWriteEnable = VK_FALSE;

	PipelineRenderingDesc renderingDesc;
	renderingDesc.colorFormats = { colorFormat };
	renderingDesc.depthFormat = depthFormat;
	_pipeline = make_unique<Pipeline>(device, renderingDesc, _shader.Get(), *_pipelineState);

	auto wireframeState = *_pipelineState;
	wireframeState.GetRasterizationStateCreateInfo().polygonMode = VK_POLYGON_MODE_LINE;
	_wireframePipeline = make_unique<Pipeline>(device, renderingDesc, _shader.Get(),
		wireframeState);
}

TerrainPass::~TerrainPass() = default;

void TerrainPass::Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
	RenderFrame& renderFrame)
{
	_active = false;

	// Needs the GPU patch list and the main targets; either missing (no
	// camera, first frames) means nothing to draw this frame.
	if (!builder.HasBuffer(TerrainPatchCullPass::SB_PATCH_LIST)
		|| !builder.HasTexture(GeometryPass::RT_MAIN_COLOR))
		return;

	_mainColor = builder.GetTexture(GeometryPass::RT_MAIN_COLOR);
	_mainDepth = builder.GetTexture(GeometryPass::RT_MAIN_DEPTH);

	FGAttachment color;
	color.texture = _mainColor;
	color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	builder.SetColorAttachment(0, color);

	FGAttachment depth;
	depth.texture = _mainDepth;
	depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	builder.SetDepthAttachment(depth);

	_camera = builder.ImportBuffer(UB_CAMERA,
		frameResources.GetOrCreateUniformBuffer<CameraBuffer>(UB_CAMERA));
	builder.Read(_camera, BufferAccess::UniformVertex);

	_patchList = builder.GetBuffer(TerrainPatchCullPass::SB_PATCH_LIST);
	builder.Read(_patchList, BufferAccess::StorageVertexRead);

	_patchDrawArgs = builder.GetBuffer(TerrainNodeListPass::SB_PATCH_DRAW_ARGS);
	builder.Read(_patchDrawArgs, BufferAccess::IndirectRead);

	TerrainParams params = _terrain.BuildRenderParams(_renderScene.GetScene().GetMainLight());
	auto paramsHandle = frameResources.GetOrCreateUniformBuffer<TerrainParams>("Terrain.Params");
	paramsHandle.Get().Update(params);
	_params = builder.ImportBuffer("Terrain.Params", paramsHandle);
	builder.Read(_params, BufferAccess::UniformFragment);

	_active = true;
}

void TerrainPass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
	if (!_active)
		return;

	context.BeginRendering(commandBuffer);
	commandBuffer.SetViewportAndScissor(context.GetRenderArea());

	Pipeline* pipeline = _terrain.IsWireframe()
		? _wireframePipeline.get() : _pipeline.get();
	commandBuffer.BindPipeline(pipeline);

	// The atlases are not graph resources: they live in GENERAL layout forever
	// (the transfer queue streams tiles into them while other slots are
	// sampled), read-only in here and ordered against the frame via the
	// transfer-timeline gate.
	TerrainQuadTree& quadTree = _terrain.GetQuadTree();

	Shader& shader = _shader.Get();
	auto builder = context.CreateDescriptorSetBuilder(shader, 0);
	builder.SetUniformBuffer(0, context.GetBuffer(_camera));
	builder.SetStorageBuffer(1, context.GetBuffer(_patchList));
	builder.SetTextureBuffer(2, quadTree.GetHeightAtlas().Get(), 0, VK_IMAGE_LAYOUT_GENERAL);
	builder.SetTextureBuffer(3, quadTree.GetNormalAtlas().Get(), 0, VK_IMAGE_LAYOUT_GENERAL);
	builder.SetTextureBuffer(4, quadTree.GetAlbedoAtlas().Get(), 0, VK_IMAGE_LAYOUT_GENERAL);
	builder.SetUniformBuffer(5, context.GetBuffer(_params));
	auto& resources = builder.Build();

	commandBuffer.BindDescriptorSet(pipeline->GetPipelineBindPoint(), shader, resources);

	commandBuffer.BindIndexBuffer(_terrain.GetGridIndexBuffer().Get(), VK_INDEX_TYPE_UINT16);

	// One instanced draw of the whole terrain; the instance count is the
	// patch culler's atomic tally, read by the GPU from the args buffer.
	commandBuffer.DrawIndexedIndirect(context.GetBuffer(_patchDrawArgs), 1,
		sizeof(VkDrawIndexedIndirectCommand));

	context.EndRendering(commandBuffer);
}

void TerrainPass::OnGUI(RenderFrame& renderFrame)
{
	if (!ImGui::CollapsingHeader("Terrain"))
		return;
	_terrain.OnGUI();
}
