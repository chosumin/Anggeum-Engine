#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/ResourceHandle.h"
#include "Graphics/Terrain/TerrainPatchCuller.h"

namespace Core
{
	class RenderScene;
	class TerrainSystem;

	// The camera's patch cull: frustum + previous-frame Hi-Z, writing the
	// traversal's draw args.
	class TerrainPatchCullPass : public FrameGraphPass
	{
	public:
		static constexpr const char* SB_PATCH_LIST = "Terrain.PatchList";

		TerrainPatchCullPass(RenderScene& renderScene, TerrainPatchCuller& culler,
			VkExtent2D screenExtent);

		const char* GetName() const override { return "TerrainPatchCull"; }

		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

	private:
		RenderScene& _renderScene;
		TerrainSystem& _terrain;
		TerrainPatchCuller& _culler;
		VkExtent2D _screenExtent{};

		TerrainPatchCuller::Inputs _inputs;
		TerrainPatchCuller::Output _output;
		FGTexture _hiZ;
		FGBuffer _cullData, _readback;
		TerrainTraversalPush _push{};
		bool _active = false;
		bool _hiZBound = false;
	};
}
