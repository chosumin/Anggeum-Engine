#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"

namespace Core
{
	class TerrainRenderer;

	// Pass-1 terrain depth draw, split from the mesh prepass so the latter does
	// not wait on the compute terrain chain; only this pass does.
	class TerrainDepthPrePass : public FrameGraphPass
	{
	public:
		explicit TerrainDepthPrePass(TerrainRenderer& terrainRenderer);
		~TerrainDepthPrePass() override;

		const char* GetName() const override { return "TerrainDepthPre1"; }

		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

	private:
		TerrainRenderer& _terrainRenderer;
		bool _active = false;
	};
}
