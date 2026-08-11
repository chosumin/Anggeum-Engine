#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
	class Device;
	class RenderScene;
	class TerrainSystem;
	class Shader;
	class Pipeline;
	class Buffer;

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

		TerrainNodeListPass(Device& device, RenderScene& renderScene);
		~TerrainNodeListPass() override;

		const char* GetName() const override { return "TerrainNodeList"; }

		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

	private:
		// Mirrors the push_constant block in terrainNodeList.comp.
		struct PushData
		{
			vec2 cameraXZ{};
			vec2 worldOrigin{};
			float rootNodeSize = 0.0f;
			float ringRadiusScale = 0.0f;
			uint32_t lodCount = 0;
			uint32_t rootTiles = 0;
		};

		RenderScene& _renderScene;
		TerrainSystem& _terrain;

		Handle<Shader> _shader;
		// Owned directly (not the pooled LoadComputePipeline) because it is
		// specialized with this config's constants.
		unique_ptr<Pipeline> _pipeline;

		// Host-visible, one per frame slot: the GPU writes this frame's node
		// count, the CPU reads the same slot two frames later (fence-safe).
		array<Handle<Buffer>, MAX_FRAMES_IN_FLIGHT> _countReadback;

		FGTexture _indexTexture;
		FGBuffer _nodeList, _nodeListCount, _readback;
		PushData _push{};
		bool _active = false;
	};
}
