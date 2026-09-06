#include "stdafx.h"
#include "DepthPrePasses.h"
#include "HiZCullPass.h"
#include "DepthPrePass.h"
#include "ResolvePass.h"
#include "TerrainPatchCullPass.h"
#include "TerrainDepthPrePass.h"
#include "Graphics/FrameGraph/FrameGraph.h"

using namespace Core;

DepthPrePasses::DepthPrePasses(FrameGraph& graph, Device& device, ResourceManager& resourceManager, RenderScene& renderScene,
	VkExtent2D extent, VkFormat depthFormat, VkSampleCountFlagBits msaaSamples)
{
	using CullPhase = HiZCullPass::Phase;
	using DepthPhase = DepthPrePass::Phase;

	auto hiZCull1 = make_unique<HiZCullPass>(device, resourceManager, renderScene, extent, CullPhase::Cull1);
	HiZCullPass* hiZCull1Ptr = hiZCull1.get();

	graph.AddPass(std::move(hiZCull1));

	// Terrain joins the prepass here. Patch culling tests against the pyramid
	// Cull1 just rebuilt (previous frame's depth, terrain included); the
	// terrain depth draw then lands before the resolve, so this frame's Hi-Z
	// rebuild in Cull2 already sees terrain as an occluder.
	graph.AddPass(make_unique<TerrainPatchCullPass>(device, resourceManager, renderScene, extent));

	graph.AddPass(make_unique<DepthPrePass>(device, resourceManager, renderScene, extent, depthFormat,
		msaaSamples, DepthPhase::First));
	graph.AddPass(make_unique<TerrainDepthPrePass>(device, resourceManager, renderScene, depthFormat,
		msaaSamples));
	graph.AddPass(make_unique<ResolvePass>(device, resourceManager, extent, msaaSamples, /*resolveNormal*/ false));
	graph.AddPass(make_unique<HiZCullPass>(device, resourceManager, renderScene, extent, CullPhase::Cull2, hiZCull1Ptr));
	graph.AddPass(make_unique<DepthPrePass>(device, resourceManager, renderScene, extent, depthFormat,
		msaaSamples, DepthPhase::Second));

	if (msaaSamples != VK_SAMPLE_COUNT_1_BIT)
		graph.AddPass(make_unique<ResolvePass>(device, resourceManager, extent, msaaSamples, /*resolveNormal*/ true));
}
