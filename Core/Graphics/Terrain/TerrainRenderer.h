#pragma once
#include "Graphics/FrameGraph/FrameGraphResource.h"
#include "Graphics/ResourceHandle.h"
#include "Graphics/BufferObjects.h"

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
	class Buffer;

	// Terrain's draws inside the shared passes. The host pass owns the
	// attachments; this declares what a draw reads and records it.
	class TerrainRenderer
	{
	public:
		TerrainRenderer(Device& device, ResourceManager& resourceManager, RenderScene& renderScene,
			VkExtent2D screenExtent, VkFormat colorFormat, VkFormat depthFormat,
			VkSampleCountFlagBits msaaSamples);
		~TerrainRenderer();

		// Depth prepass draw (depth + packed normal). False when there is no
		// patch list this frame; Record then draws nothing.
		bool SetupDepth(FrameGraphBuilder& builder, FrameResources& frameResources);
		void RecordDepth(FrameGraphPassContext& context, CommandBuffer& commandBuffer);

		// Color draw, early-z against the prepass depth with writes off.
		bool SetupColor(FrameGraphBuilder& builder, FrameResources& frameResources);
		void RecordColor(FrameGraphPassContext& context, CommandBuffer& commandBuffer);

		// Shadow cascades: depth-only draws of the per-cascade patch lists the
		// shadow cull produced (absent lists leave the cascade without terrain).
		void SetupShadow(FrameGraphBuilder& builder, FrameResources& frameResources,
			uint32_t cascadeCount);
		void RecordShadow(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
			uint32_t cascade, Buffer& cascadeCamera);

	private:
		bool SetupShared(FrameGraphBuilder& builder, FrameResources& frameResources);
		FGBuffer SetupParams(FrameGraphBuilder& builder, FrameResources& frameResources);
		void BindShared(FrameGraphPassContext& context, DescriptorSetBuilder& builder);

		RenderScene& _renderScene;
		TerrainSystem& _terrain;
		VkExtent2D _screenExtent{};

		Handle<Shader> _depthShader;
		unique_ptr<PipelineState> _depthPipelineState;
		unique_ptr<Pipeline> _depthPipeline;

		Handle<Shader> _colorShader;
		unique_ptr<PipelineState> _colorPipelineState;
		unique_ptr<Pipeline> _colorPipeline;
		unique_ptr<Pipeline> _wireframePipeline;

		Handle<Shader> _shadowShader;
		unique_ptr<PipelineState> _shadowPipelineState;
		unique_ptr<Pipeline> _shadowPipeline;

		FGBuffer _camera, _patchList, _patchDrawArgs, _params, _pick;
		bool _depthActive = false;
		bool _colorActive = false;

		// Color draw shadow inputs (the SDF mask may be absent).
		FGBuffer _shadowUB;
		FGTexture _shadowMap, _sdfShadow;

		FGBuffer _shadowParams;
		array<FGBuffer, SHADOW_MAP_CASCADE_COUNT> _shadowPatchLists{};
		array<FGBuffer, SHADOW_MAP_CASCADE_COUNT> _shadowDrawArgs{};
	};
}
