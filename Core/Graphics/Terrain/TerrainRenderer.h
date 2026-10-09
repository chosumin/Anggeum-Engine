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

		// The two patch lists of the two-phase cull.
		enum class Phase { First, Second };

		// Depth prepass draw (depth + packed normal) of one phase's list.
		// False when that list does not exist this frame; Record then draws nothing.
		bool SetupDepth(FrameGraphBuilder& builder, FrameResources& frameResources, Phase phase);
		void RecordDepth(FrameGraphPassContext& context, CommandBuffer& commandBuffer, Phase phase);

		// Color draw of both lists, early-z against the prepass depth with writes off.
		bool SetupColor(FrameGraphBuilder& builder, FrameResources& frameResources);
		void RecordColor(FrameGraphPassContext& context, CommandBuffer& commandBuffer);

		// Shadow cascades: depth-only draws of the per-cascade patch lists the
		// shadow cull produced (absent lists leave the cascade without terrain).
		void SetupShadow(FrameGraphBuilder& builder, FrameResources& frameResources,
			uint32_t cascadeCount);
		void RecordShadow(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
			uint32_t cascade, Buffer& cascadeCamera);
		bool HasShadowList(uint32_t cascade) const { return _shadowPatchLists[cascade].IsValid(); }

	private:
		// A phase's list + args; false when the cull produced none this frame.
		struct PatchList
		{
			FGBuffer patches, drawArgs;
			bool IsValid() const { return patches.IsValid(); }
		};

		bool SetupShared(FrameGraphBuilder& builder, FrameResources& frameResources);
		PatchList SetupPatchList(FrameGraphBuilder& builder, const char* listName,
			const char* argsName);
		FGBuffer SetupParams(FrameGraphBuilder& builder, FrameResources& frameResources);
		void BindShared(FrameGraphPassContext& context, DescriptorSetBuilder& builder,
			const PatchList& list);
		void DrawList(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
			const PatchList& list);

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

		FGBuffer _camera, _params, _pick;
		PatchList _lists[2];           // indexed by Phase
		bool _colorActive = false;

		// Color draw shadow inputs (the SDF mask may be absent).
		FGBuffer _shadowUB;
		FGTexture _shadowMap, _sdfShadow;

		FGBuffer _shadowParams;
		array<FGBuffer, SHADOW_MAP_CASCADE_COUNT> _shadowPatchLists{};
		array<FGBuffer, SHADOW_MAP_CASCADE_COUNT> _shadowDrawArgs{};
	};
}
