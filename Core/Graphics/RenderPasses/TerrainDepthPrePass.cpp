#include "stdafx.h"
#include "TerrainDepthPrePass.h"
#include "DepthPrePass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/Terrain/TerrainRenderer.h"
#include "Graphics/Vulkans/CommandBuffer.h"

using namespace Core;

TerrainDepthPrePass::TerrainDepthPrePass(TerrainRenderer& terrainRenderer)
	: _terrainRenderer(terrainRenderer)
{
}

TerrainDepthPrePass::~TerrainDepthPrePass() = default;

void TerrainDepthPrePass::Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
	RenderFrame& renderFrame)
{
	// No list this frame: declare nothing so the graph culls the pass.
	_active = _terrainRenderer.SetupDepth(builder, frameResources, TerrainRenderer::Phase::First);
	if (!_active)
		return;

	FGAttachment color;
	color.texture = builder.GetTexture(DepthPrePass::RT_MAIN_NORMAL);
	color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	builder.SetColorAttachment(0, color);

	FGAttachment depth;
	depth.texture = builder.GetTexture(DepthPrePass::RT_MAIN_DEPTH);
	depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	builder.SetDepthAttachment(depth);
}

void TerrainDepthPrePass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
	if (!_active)
		return;

	context.BeginRendering(commandBuffer);
	commandBuffer.SetViewportAndScissor(context.GetRenderArea());
	_terrainRenderer.RecordDepth(context, commandBuffer, TerrainRenderer::Phase::First);
	context.EndRendering(commandBuffer);
}
