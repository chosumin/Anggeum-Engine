#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"

namespace Core
{
	class Device;
	class TerrainSystem;

	// Records this frame's budgeted tile uploads into the terrain atlases and
	// lookup tables. Registered first in the graph so uploads land before any
	// terrain sampling. Declares nothing (and is culled) on quiet frames.
	class TerrainStreamingPass : public FrameGraphPass
	{
	public:
		TerrainStreamingPass(TerrainSystem& terrain) : _terrain(terrain) {}

		const char* GetName() const override { return "TerrainStreaming"; }

		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;
		void OnGUI(RenderFrame& renderFrame) override;

	private:
		TerrainSystem& _terrain;

		FGTexture _height, _normal, _albedo, _indexTexture;
		FGBuffer _nodeDesc;
	};
}
