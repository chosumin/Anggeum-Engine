#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/ResourceHandle.h"
#include "Graphics/Terrain/TerrainConfig.h"

namespace Core
{
	class ResourceManager;
	class Device;
	class RenderScene;
	class TerrainSystem;
	class Shader;
	class Pipeline;
	class Buffer;
	class Texture;

	// Phase-2 GPU pipeline, stage 1: builds the Terrain Node List on the GPU
	// (FC5's covering-set traversal) from the quadtree index texture. Runs
	// alongside the CPU traversal for now; the CPU render list keeps driving
	// the actual draw until the patch pipeline replaces it. The node count is
	// read back (2 frames latency) purely to validate against the CPU count.
	class TerrainNodeListPass : public FrameGraphPass
	{
	public:
		static constexpr const char* SB_NODE_LIST = "Terrain.NodeList";
		static constexpr const char* SB_NODE_LIST_COUNT = "Terrain.NodeListCount";
		static constexpr const char* SB_PATCH_DRAW_ARGS = "Terrain.PatchDrawArgs";

		TerrainNodeListPass(Device& device, ResourceManager& resourceManager, RenderScene& renderScene);
		~TerrainNodeListPass() override;

		const char* GetName() const override { return "TerrainNodeList"; }

		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

	private:
		Device& _device;
		RenderScene& _renderScene;
		TerrainSystem& _terrain;

		Handle<Shader> _shader;
		// Owned directly (not the pooled LoadComputePipeline) because it is
		// specialized with this config's constants.
		unique_ptr<Pipeline> _pipeline;

		Handle<Texture> _indexTexture;

		FGBuffer _nodeList, _nodeListCount, _patchDrawArgs;
		TerrainTraversalPush _push{};
		bool _active = false;
	};
}
