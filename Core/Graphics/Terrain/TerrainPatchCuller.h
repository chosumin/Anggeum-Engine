#pragma once
#include "Graphics/FrameGraph/FrameGraphResource.h"
#include "Graphics/ResourceHandle.h"
#include "TerrainConfig.h"

namespace Core
{
	class ResourceManager;
	class TerrainSystem;
	class Shader;
	class Pipeline;
	class Buffer;
	class Texture;
	class PerspectiveCamera;
	class FrameGraphBuilder;
	class FrameResources;
	class FrameGraphPassContext;
	class CommandBuffer;

	// The terrain patch cull dispatches, shared by every pass that culls the
	// node list against a view.
	class TerrainPatchCuller
	{
	public:
		// What a pass-1 cull reads: the traversal's node list and the LOD map.
		struct Inputs
		{
			FGBuffer nodeList, nodeListCount;
			FGTexture lodMap;
		};

		// One cull's patch list and the draw args it bumps. The rejected pair
		// is optional: only the camera's pass 1 feeds a pass 2.
		struct Output
		{
			FGBuffer patchList, drawArgs;
			FGBuffer rejectedList, rejectedCount;
		};

		TerrainPatchCuller(ResourceManager& resourceManager, TerrainSystem& terrain);
		~TerrainPatchCuller();

		Inputs SetupInputs(FrameGraphBuilder& builder, FrameResources& frameResources);
		
		void AcquireNodeDescs(FrameResources& frameResources);

		FGBuffer CreatePatchList(FrameGraphBuilder& builder, const string& name);
		FGBuffer CreateDrawArgs(FrameGraphBuilder& builder, const string& name);
		FGBuffer CreateRejectedList(FrameGraphBuilder& builder, const string& name);
		FGBuffer CreateRejectedCount(FrameGraphBuilder& builder, const string& name);

		TerrainTraversalPush BuildPush(const PerspectiveCamera& camera) const;

		// Fill-based: indexCount = patch index count, the rest zero.
		void ResetDrawArgs(CommandBuffer& commandBuffer, Buffer& drawArgs) const;

		void Dispatch(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
			const Inputs& inputs, const Output& output, Buffer& cullData,
			Texture* hiZ, const TerrainTraversalPush& push);

		// Re-tests pass 1's rejected patches against this frame's Hi-Z.
		void DispatchPass2(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
			FGBuffer rejectedList, FGBuffer rejectedCount, const Output& output,
			Buffer& cullData, Texture& hiZ, const TerrainTraversalPush& push);

	private:
		TerrainSystem& _terrain;

		Handle<Shader> _shader;
		Handle<Pipeline> _pipeline;
		Handle<Shader> _pass2Shader;
		Handle<Pipeline> _pass2Pipeline;
		Handle<Buffer> _nodeDescBuffer;
	};
}
