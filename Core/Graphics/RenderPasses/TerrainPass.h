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

	class TerrainPass : public FrameGraphPass
	{
	public:
		TerrainPass(Device& device, RenderScene& renderScene,
			VkFormat colorFormat, VkFormat depthFormat,
			VkSampleCountFlagBits msaaSamples);
		~TerrainPass() override;

		const char* GetName() const override { return "Terrain"; }

		void Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
			RenderFrame& renderFrame) override;
		void Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer) override;

	private:
		Device& _device;
		RenderScene& _renderScene;
		TerrainSystem& _terrain;

		Handle<Shader> _shader;
		unique_ptr<PipelineState> _pipelineState;
		unique_ptr<Pipeline> _pipeline;
		unique_ptr<Pipeline> _wireframePipeline;

		FGTexture _mainColor, _mainDepth, _height, _normal, _albedo;
		FGBuffer _camera, _instances, _params;
		uint32_t _instanceCount = 0;
	};
}
