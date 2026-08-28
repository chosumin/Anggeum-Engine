#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/ResourceHandle.h"
#include "Graphics/Terrain/TerrainConfig.h"

namespace Core
{
	class Device;
	class RenderScene;
	class TerrainSystem;
	class Shader;
	class Pipeline;
	class Buffer;

	// Expands the Terrain Node List into 8x8 patches per node,
	// culls each patch (frustum + previous-frame Hi-Z) and
	// packs LOD stitch deltas from the LOD map, producing the Visible Render
	// Patch List + the instanced draw's indirect args.
	class TerrainPatchCullPass : public FrameGraphPass
	{
	public:
		static constexpr const char* SB_PATCH_LIST = "Terrain.PatchList";

		TerrainPatchCullPass(Device& device, RenderScene& renderScene,
			VkExtent2D screenExtent);

		const char* GetName() const override { return "TerrainPatchCull"; }

		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

	private:
		RenderScene& _renderScene;
		TerrainSystem& _terrain;
		VkExtent2D _screenExtent{};

		Handle<Shader> _shader;
		Handle<Pipeline> _pipeline;

		FGTexture _lodMap, _hiZ;
		FGBuffer _nodeList, _nodeListCount, _patchList,
			_patchDrawArgs, _cullData, _readback;
		TerrainTraversalPush _push{};
		bool _active = false;
		bool _hiZBound = false;
		bool _occlusionEnabled = false;
	};
}
