#include "stdafx.h"
#include "DepthPrePasses.h"
#include "HiZCullPass.h"
#include "DepthPrePass.h"
#include "ResolvePass.h"
#include "TerrainPatchCullPass.h"
#include "Graphics/FrameGraph/FrameGraph.h"

using namespace Core;

DepthPrePasses::DepthPrePasses(FrameGraph& graph, Device& device, ResourceManager& resourceManager, RenderScene& renderScene,
	TerrainRenderer& terrainRenderer, TerrainPatchCuller& terrainCuller, VkExtent2D extent,
	VkFormat depthFormat, VkSampleCountFlagBits msaaSamples)
{
	using CullPhase = HiZCullPass::Phase;
	using DepthPhase = DepthPrePass::Phase;

	auto hiZCull1 = make_unique<HiZCullPass>(device, resourceManager, renderScene, extent, CullPhase::Cull1);
	HiZCullPass* hiZCull1Ptr = hiZCull1.get();

	graph.AddPass(std::move(hiZCull1));

	// Terrain patch culling tests against the pyramid Cull1 just rebuilt
	// (previous frame's depth, terrain included); DepthPre1 draws the patches.
	graph.AddPass(make_unique<TerrainPatchCullPass>(renderScene, terrainCuller, extent));

	graph.AddPass(make_unique<DepthPrePass>(device, resourceManager, renderScene, terrainRenderer,
		extent, depthFormat, msaaSamples, DepthPhase::First));
	graph.AddPass(make_unique<ResolvePass>(device, resourceManager, extent, msaaSamples, /*resolveNormal*/ false));
	graph.AddPass(make_unique<HiZCullPass>(device, resourceManager, renderScene, extent, CullPhase::Cull2, hiZCull1Ptr));
	graph.AddPass(make_unique<DepthPrePass>(device, resourceManager, renderScene, terrainRenderer,
		extent, depthFormat, msaaSamples, DepthPhase::Second));

	if (msaaSamples != VK_SAMPLE_COUNT_1_BIT)
		graph.AddPass(make_unique<ResolvePass>(device, resourceManager, extent, msaaSamples, /*resolveNormal*/ true));
}
