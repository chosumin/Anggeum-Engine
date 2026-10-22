#pragma once
#include "Graphics/FrameGraph/FrameGraphPass.h"
#include "Graphics/Terrain/TerrainPatchCuller.h"
#include "Graphics/ResourceHandle.h"
#include "Graphics/BufferObjects.h"

namespace Core
{
	class RenderScene;
	class Buffer;
	class Texture;

	// Pass-1 terrain patch cull against the previous frame's Hi-Z.
	class TerrainPatchCullPass : public FrameGraphPass
	{
	public:
		static constexpr const char* SB_PATCH_LIST = "Terrain.PatchList";
		static constexpr const char* SB_REJECTED = "Terrain.RejectedPatches";
		static constexpr const char* SB_REJECTED_COUNT = "Terrain.RejectedPatchCount";

		TerrainPatchCullPass(RenderScene& renderScene, TerrainPatchCuller& terrainCuller,
			VkExtent2D screenExtent);
		~TerrainPatchCullPass() override;

		const char* GetName() const override { return "TerrainPatchCull"; }
		QueueType GetQueueType() const override { return QueueType::Compute; }

		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

	private:
		RenderScene& _renderScene;
		TerrainPatchCuller& _terrainCuller;
		VkExtent2D _extent;
		uint32_t _hiZMipLevels;

		bool _active = false;
		CameraBuffer _camera{};
		TerrainPatchCuller::Inputs _inputs;
		TerrainPatchCuller::Output _output;
		FGBuffer _readback;
		Handle<Buffer> _cullData;
		Handle<Texture> _prevHiZ; // invalid: occlusion testing off
		TerrainTraversalPush _push{};
	};
}
