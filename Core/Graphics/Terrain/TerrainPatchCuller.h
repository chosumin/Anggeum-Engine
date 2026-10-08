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

	// The terrain patch cull dispatch, shared by every pass that culls the
	// node list against a view (camera, shadow cascades).
	class TerrainPatchCuller
	{
	public:
		// What every cull reads: the traversal's node list and the LOD map.
		struct Inputs
		{
			FGBuffer nodeList, nodeListCount;
			FGTexture lodMap;
		};

		// One cull's patch list and the draw args it bumps.
		struct Output
		{
			FGBuffer patchList, drawArgs;
		};

		TerrainPatchCuller(ResourceManager& resourceManager, TerrainSystem& terrain);
		~TerrainPatchCuller();

		Inputs SetupInputs(FrameGraphBuilder& builder, FrameResources& frameResources);

		FGBuffer CreatePatchList(FrameGraphBuilder& builder, const string& name);
		FGBuffer CreateDrawArgs(FrameGraphBuilder& builder, const string& name);

		TerrainTraversalPush BuildPush(const PerspectiveCamera& camera) const;

		void ResetDrawArgs(CommandBuffer& commandBuffer, Buffer& drawArgs) const;

		void Dispatch(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
			const Inputs& inputs, const Output& output, Buffer& cullData,
			Texture* hiZ, const TerrainTraversalPush& push);

	private:
		TerrainSystem& _terrain;

		Handle<Shader> _shader;
		Handle<Pipeline> _pipeline;
		Handle<Buffer> _nodeDescBuffer;
	};
}
