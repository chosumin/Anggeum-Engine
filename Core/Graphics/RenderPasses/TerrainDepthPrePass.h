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
	class PipelineState;

	// Terrain's seat in the depth prepass: draws the GPU-culled patch list
	// depth-only (plus the packed normal MainNormal carries for AO). This puts
	// terrain into the resolved depth, so it becomes an occluder in every Hi-Z
	// consumer - the mesh pass-2 cull this frame, and both the mesh pass-1
	// cull and the terrain patch cull next frame - and leaves the color pass
	// an early-z depth buffer to test against with writes off.
	class TerrainDepthPrePass : public FrameGraphPass
	{
	public:
		TerrainDepthPrePass(Device& device, RenderScene& renderScene,
			VkFormat depthFormat, VkSampleCountFlagBits msaaSamples);
		~TerrainDepthPrePass() override;

		const char* GetName() const override { return "TerrainDepthPre"; }

		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

	private:
		RenderScene& _renderScene;
		TerrainSystem& _terrain;

		Handle<Shader> _shader;
		unique_ptr<PipelineState> _pipelineState;
		unique_ptr<Pipeline> _pipeline;

		FGTexture _mainNormal, _mainDepth;
		FGBuffer _camera, _patchList, _patchDrawArgs, _params;
		bool _active = false;
	};
}
