#include "stdafx.h"
#include "TerrainUpload.h"
#include "TerrainNodeStore.h"
#include "TerrainQuadTree.h"
#include "Graphics/StagingRing.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/MemoryAllocator.h"

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
		vector<TerrainTileUpload>&& tiles, StagingRing::Span span,
		bool initializeAtlases)
		: UploadJob()
		, _store(store)
		, _config(config)
		, _heightAtlas(quadTree.GetHeightAtlas().Get())
		, _normalAtlas(quadTree.GetNormalAtlas().Get())
		, _albedoAtlas(quadTree.GetAlbedoAtlas().Get())
		, _quadTree(quadTree)
		, _tiles(std::move(tiles))
		, _span(span)
		, _initializeAtlases(initializeAtlases)
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

		// First upload ever: the atlases leave UNDEFINED for their permanent
		// GENERAL layout here, ahead of the first copies in this very
		// recording. (Legal on the transfer family - the sanitizers collapse
		// GENERAL's shader stages/accesses; visibility for consumers is the
		// timeline gate.)
		if (_initializeAtlases)
		{
			commandBuffer->CreateBarrierBatch()
				.Image(_heightAtlas, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL)
				.Image(_normalAtlas, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL)
				.Image(_albedoAtlas, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL)
				.Submit();
		}

		commandBuffer->CopyBufferToImage(*source, _heightAtlas, heightRegions,
			VK_IMAGE_LAYOUT_GENERAL);
		commandBuffer->CopyBufferToImage(*source, _normalAtlas, normalRegions,
			VK_IMAGE_LAYOUT_GENERAL);
		commandBuffer->CopyBufferToImage(*source, _albedoAtlas, albedoRegions,
			VK_IMAGE_LAYOUT_GENERAL);
	}

	TerrainIndexUploadJob::TerrainIndexUploadJob(Device& device,
		Texture& indexTexture, const TerrainConfig& config,
		vector<vector<uint16_t>>&& indexMips)
		: Job(JobType::TRANSFER)
		, _device(device)
		, _config(config)
		, _indexTexture(indexTexture)
		, _indexMips(std::move(indexMips))
	{
	}

	TerrainIndexUploadJob::~TerrainIndexUploadJob() = default;

	void TerrainIndexUploadJob::Execute()
	{
		// One-shot staging owned by the job (inline init work never touches
		// the transfer-timeline staging ring).
		VkDeviceSize stagingBytes = 0;
		for (const auto& mip : _indexMips)
			stagingBytes = Align(stagingBytes, 16) + mip.size() * sizeof(uint16_t);

		_stagingBuffer = make_unique<Buffer>(_device, stagingBytes,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryType::DEDICATED_HOST);
		void* mappedRaw = nullptr;
		_stagingBuffer->GetMappedPtr(&mappedRaw);
		uint8_t* mapped = static_cast<uint8_t*>(mappedRaw);

		VkDeviceSize offset = 0;
		vector<VkBufferImageCopy> indexRegions;
		for (uint32_t lod = 0; lod < _config.lodCount; ++lod)
		{
			offset = Align(offset, 16);
			VkDeviceSize mipBytes = _indexMips[lod].size() * sizeof(uint16_t);
			memcpy(mapped + offset, _indexMips[lod].data(), size_t(mipBytes));
			indexRegions.push_back(MakeRegion(offset, uvec2(0),
				_config.NodesPerSide(lod), lod));
			offset += mipBytes;
		}

		// Every mip is rewritten and this frame slot's copy is idle, so the
		// old contents can be discarded (UNDEFINED) instead of preserved.
		commandBuffer->CreateBarrierBatch()
			.Image(_indexTexture, VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
			.Submit();

		commandBuffer->CopyBufferToImage(*_stagingBuffer, _indexTexture, indexRegions);

		commandBuffer->CreateBarrierBatch()
			.Image(_indexTexture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			.Submit();

		status = JobStatus::COMPLETE;
	}
}
