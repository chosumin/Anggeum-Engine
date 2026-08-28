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
	class Sampler;

	class TerrainLodMapPass : public FrameGraphPass
	{
	public:
		static constexpr const char* RT_LOD_MAP = "Terrain.LodMap";

		TerrainLodMapPass(Device& device, RenderScene& renderScene);
		~TerrainLodMapPass() override;

		const char* GetName() const override { return "TerrainLodMap"; }

		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

	private:
		RenderScene& _renderScene;
		TerrainSystem& _terrain;

		Handle<Shader> _shader;
		Handle<Pipeline> _pipeline;

		Handle<Sampler> _nearestSampler;

		FGTexture _lodMap;
		TerrainTraversalPush _push{};
		uint32_t _sectorsPerSide = 0;
		bool _active = false;
	};
}
