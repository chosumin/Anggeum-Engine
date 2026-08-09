#include "stdafx.h"
#include "TerrainStreamingPass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/Terrain/TerrainSystem.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Buffer.h"

namespace Core
{
	void TerrainStreamingPass::Setup(FrameGraphBuilder& builder,
		FrameResources& frameResources, RenderFrame& renderFrame)
	{
		const auto& uploads = _terrain.GetFrameUploads();
		if (uploads.Empty())
			return; // nothing this frame; the pass culls itself

		TerrainQuadTree& quadTree = _terrain.GetQuadTree();

		_height = _normal = _albedo = _indexTexture = {};
		_nodeDesc = {};

		if (!uploads.heightRegions.empty())
		{
			_height = builder.ImportTexture(TerrainQuadTree::HEIGHT_ATLAS, quadTree.GetHeightAtlas());
			_normal = builder.ImportTexture(TerrainQuadTree::NORMAL_ATLAS, quadTree.GetNormalAtlas());
			_albedo = builder.ImportTexture(TerrainQuadTree::ALBEDO_ATLAS, quadTree.GetAlbedoAtlas());
			builder.Write(_height, TextureAccess::TransferDst);
			builder.Write(_normal, TextureAccess::TransferDst);
			builder.Write(_albedo, TextureAccess::TransferDst);
		}

		if (!uploads.indexRegions.empty())
		{
			_indexTexture = builder.ImportTexture(TerrainQuadTree::QUADTREE_INDEX,
				quadTree.GetIndexTexture());
			builder.Write(_indexTexture, TextureAccess::TransferDst);
		}

		if (uploads.descDirty)
		{
			_nodeDesc = builder.ImportBuffer(TerrainQuadTree::NODE_DESC,
				quadTree.GetNodeDescBuffer());
			builder.Write(_nodeDesc, BufferAccess::TransferDst);
		}

		// The atlases outlive the frame: uploads must land even on frames
		// where no terrain pass consumes them (e.g. before the first draw).
		builder.SetSideEffect();
	}

	void TerrainStreamingPass::Execute(FrameGraphPassContext& context,
		CommandBuffer& commandBuffer)
	{
		const auto& uploads = _terrain.GetFrameUploads();
		Buffer& staging = *uploads.staging;

		if (!uploads.heightRegions.empty())
		{
			commandBuffer.CopyBufferToImage(staging,
				context.GetTexture(_height), uploads.heightRegions);
			commandBuffer.CopyBufferToImage(staging,
				context.GetTexture(_normal), uploads.normalRegions);
			commandBuffer.CopyBufferToImage(staging,
				context.GetTexture(_albedo), uploads.albedoRegions);
		}

		if (!uploads.indexRegions.empty())
			commandBuffer.CopyBufferToImage(staging,
				context.GetTexture(_indexTexture), uploads.indexRegions);

		if (uploads.descDirty)
			commandBuffer.CopyBuffer(staging, context.GetBuffer(_nodeDesc),
				0, uploads.descOffset, context.GetBuffer(_nodeDesc).GetSize());
	}

	void TerrainStreamingPass::OnGUI(RenderFrame& renderFrame)
	{
		if (!ImGui::CollapsingHeader("Terrain"))
			return;
		_terrain.OnGUI();
	}
}
