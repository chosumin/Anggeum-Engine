#pragma once
#include "Graphics/FrameGraph/FrameGraphResource.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
	class Device;
	class ResourceManager;
	class RenderScene;
	class TerrainSystem;
	class Shader;
	class Pipeline;
	class PipelineState;
	class FrameGraphBuilder;
	class FrameResources;
	class FrameGraphPassContext;
	class CommandBuffer;
	class DescriptorSetBuilder;

	// Terrain's draws inside the shared passes. The host pass owns the
	// attachments; this declares what a draw reads and records it.
	class TerrainRenderer
	{
	public:
		TerrainRenderer(Device& device, ResourceManager& resourceManager, RenderScene& renderScene,
			VkFormat colorFormat, VkFormat depthFormat, VkSampleCountFlagBits msaaSamples);
		~TerrainRenderer();

		// Depth prepass draw (depth + packed normal). False when there is no
		// patch list this frame; Record then draws nothing.
		bool SetupDepth(FrameGraphBuilder& builder, FrameResources& frameResources);
		void RecordDepth(FrameGraphPassContext& context, CommandBuffer& commandBuffer);

		// Color draw, early-z against the prepass depth with writes off.
		bool SetupColor(FrameGraphBuilder& builder, FrameResources& frameResources);
		void RecordColor(FrameGraphPassContext& context, CommandBuffer& commandBuffer);

	private:
		bool SetupShared(FrameGraphBuilder& builder, FrameResources& frameResources);
		void BindShared(FrameGraphPassContext& context, DescriptorSetBuilder& builder);

		RenderScene& _renderScene;
		TerrainSystem& _terrain;

		Handle<Shader> _depthShader;
		unique_ptr<PipelineState> _depthPipelineState;
		unique_ptr<Pipeline> _depthPipeline;

		Handle<Shader> _colorShader;
		unique_ptr<PipelineState> _colorPipelineState;
		unique_ptr<Pipeline> _colorPipeline;
		unique_ptr<Pipeline> _wireframePipeline;

		FGBuffer _camera, _patchList, _patchDrawArgs, _params, _pick;
		bool _depthActive = false;
		bool _colorActive = false;
	};
}
