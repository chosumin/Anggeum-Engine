#pragma once

namespace Core
{
	class ResourceManager;
	class FrameGraph;
	class Device;
	class RenderScene;

	// Bundles the depth prepass chain: two-pass occlusion culling interleaved
	// with the split depth prepass and the depth/normal resolves.
	class DepthPrePasses
	{
	public:
		DepthPrePasses(FrameGraph& graph, Device& device, ResourceManager& resourceManager, RenderScene& renderScene,
			VkExtent2D extent, VkFormat depthFormat, VkSampleCountFlagBits msaaSamples);
	};
}
