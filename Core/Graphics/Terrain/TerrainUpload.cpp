#include "stdafx.h"
#include "TerrainUpload.h"
#include "TerrainNodeStore.h"
#include "TerrainQuadTree.h"
#include "Graphics/StagingRing.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/CommandBuffer.h"

namespace
{
	VkDeviceSize Align(VkDeviceSize offset, VkDeviceSize alignment)
	{
		return (offset + alignment - 1) & ~(alignment - 1);
	}

	VkBufferImageCopy MakeRegion(VkDeviceSize bufferOffset, glm::uvec2 texelOrigin,
		uint32_t extent, uint32_t mipLevel = 0)
	{
		VkBufferImageCopy region{};
		region.bufferOffset = bufferOffset;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = mipLevel;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = { int32_t(texelOrigin.x), int32_t(texelOrigin.y), 0 };
		region.imageExtent = { extent, extent, 1 };
		return region;
	}
}

namespace Core
{
	TerrainUploadJob::TerrainUploadJob(TerrainQuadTree& quadTree,
		const TerrainNodeStore& store, const TerrainConfig& config,
		vector<TerrainTileUpload>&& tiles, bool tablesDirty,
		vector<TerrainNodeDescGPU>&& descSnapshot,
		vector<vector<uint16_t>>&& indexSnapshot, bool firstUpload,
		StagingRing::Span span)
		: UploadJob()
		, _store(store)
		, _config(config)
		, _heightAtlas(quadTree.GetHeightAtlas().Get())
		, _normalAtlas(quadTree.GetNormalAtlas().Get())
		, _albedoAtlas(quadTree.GetAlbedoAtlas().Get())
		, _indexTexture(quadTree.GetIndexTexture().Get())
		, _nodeDescBuffer(quadTree.GetNodeDescBuffer().Get())
		, _quadTree(quadTree)
		, _tiles(std::move(tiles))
		, _tablesDirty(tablesDirty)
		, _descSnapshot(std::move(descSnapshot))
		, _indexSnapshot(std::move(indexSnapshot))
		, _firstUpload(firstUpload)
		, _span(span)
	{
		stagingSpanId = span.id;
	}

	TerrainUploadJob::~TerrainUploadJob() = default;

	void TerrainUploadJob::Execute()
	{
		uint32_t heightTexels = _config.HeightTexels();
		uint32_t colorTexels = _config.ColorTexels();

		// Regions carry offsets into the ring buffer, so rebase the write
		// pointer to the buffer's origin and append at absolute offsets.
		uint8_t* stagingBase = _span.mapped - _span.offset;
		Buffer* source = _span.buffer;
		VkDeviceSize offset = _span.offset;

		// --- Stage tile payloads + build copy regions ---
		vector<VkBufferImageCopy> heightRegions, normalRegions, albedoRegions;
		heightRegions.reserve(_tiles.size());
		normalRegions.reserve(_tiles.size());
		albedoRegions.reserve(_tiles.size());

		auto append = [&](const void* data, VkDeviceSize bytes,
			vector<VkBufferImageCopy>& regions, uvec2 texelOrigin, uint32_t extent)
			{
				offset = Align(offset, 16);
				memcpy(stagingBase + offset, data, size_t(bytes));
				regions.push_back(MakeRegion(offset, texelOrigin, extent));
				offset += bytes;
			};

		for (const TerrainTileUpload& tile : _tiles)
		{
			const TerrainNodePayload& payload = _store.Get(tile.id);
			append(payload.height.data(), payload.height.size() * 2,
				heightRegions, _quadTree.HeightTexelOrigin(tile.slot), heightTexels);
			append(payload.normal.data(), payload.normal.size() * 4,
				normalRegions, _quadTree.ColorTexelOrigin(tile.slot), colorTexels);
			append(payload.albedo.data(), payload.albedo.size() * 4,
				albedoRegions, _quadTree.ColorTexelOrigin(tile.slot), colorTexels);
		}

		// --- Stage lookup tables ---
		vector<VkBufferImageCopy> indexRegions;
		VkDeviceSize descOffset = 0;
		if (_tablesDirty)
		{
			offset = Align(offset, 16);
			descOffset = offset;
			VkDeviceSize descBytes = _descSnapshot.size() * sizeof(TerrainNodeDescGPU);
			memcpy(stagingBase + offset, _descSnapshot.data(), size_t(descBytes));
			offset += descBytes;

			for (uint32_t lod = 0; lod < _config.lodCount; ++lod)
			{
				offset = Align(offset, 16);
				VkDeviceSize mipBytes = _indexSnapshot[lod].size() * sizeof(uint16_t);
				memcpy(stagingBase + offset, _indexSnapshot[lod].data(), size_t(mipBytes));
				indexRegions.push_back(MakeRegion(offset, uvec2(0),
					_config.NodesPerSide(lod), lod));
				offset += mipBytes;
			}
		}

		// --- Record: transition, copy, transition back (net-zero for the
		// frame graph, which believes these images stay SHADER_READ_ONLY) ---
		bool touchAtlases = _firstUpload || !heightRegions.empty();
		bool touchIndex = _firstUpload || !indexRegions.empty();
		VkImageLayout oldLayout = _firstUpload
			? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		{
			auto barrier = commandBuffer->CreateBarrierBatch();
			if (touchAtlases)
			{
				barrier.Image(_heightAtlas, oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
				barrier.Image(_normalAtlas, oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
				barrier.Image(_albedoAtlas, oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
			}
			if (touchIndex)
				barrier.Image(_indexTexture, oldLayout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
			barrier.Submit();
		}

		if (!heightRegions.empty())
		{
			commandBuffer->CopyBufferToImage(*source, _heightAtlas, heightRegions);
			commandBuffer->CopyBufferToImage(*source, _normalAtlas, normalRegions);
			commandBuffer->CopyBufferToImage(*source, _albedoAtlas, albedoRegions);
		}

		if (!indexRegions.empty())
			commandBuffer->CopyBufferToImage(*source, _indexTexture, indexRegions);

		if (_tablesDirty)
			commandBuffer->CopyBuffer(*source, _nodeDescBuffer,
				0, descOffset, _nodeDescBuffer.GetSize());

		{
			auto barrier = commandBuffer->CreateBarrierBatch();
			if (touchAtlases)
			{
				barrier.Image(_heightAtlas, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				barrier.Image(_normalAtlas, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				barrier.Image(_albedoAtlas, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			}
			if (touchIndex)
				barrier.Image(_indexTexture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
			barrier.Submit();
		}

		status = JobStatus::COMPLETE;
	}
}
