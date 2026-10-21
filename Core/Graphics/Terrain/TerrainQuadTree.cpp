#include "stdafx.h"
#include "TerrainQuadTree.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/Vulkans/Image.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Foundation/Job.h"

namespace
{
	using namespace Core;

	// The atlases live in GENERAL for the whole run. Done once, blocking, at
	// creation: a terrain draw can bind them before the first tile upload is
	// even submitted, and a transfer-queue job cannot order itself ahead of it.
	class AtlasLayoutJob : public Job
	{
	public:
		explicit AtlasLayoutJob(vector<Texture*> atlases)
			: Job(JobType::GRAPHICS_PRIMARY), _atlases(std::move(atlases))
		{
		}

		void Execute() override
		{
			auto batch = commandBuffer->CreateBarrierBatch();
			for (Texture* atlas : _atlases)
			{
				batch.Image(*atlas, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
					VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
					VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
					VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
			}
			batch.Submit();
		}

	private:
		vector<Texture*> _atlases;
	};

	Handle<Texture> CreateAtlasTexture(Device& device,
		ResourceManager& resourceManager, const char* name,
		uvec2 extent, VkFormat format, uint32_t mipLevels = 1,
		// Integer formats (the quadtree index, the splat IDs) cannot be linearly filtered.
		VkFilter filter = VK_FILTER_LINEAR)
	{
		ImageDesc desc{};
		desc.extent = { extent.x, extent.y };
		desc.format = format;
		desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		desc.mipLevels = mipLevels;

		auto sampler = resourceManager.LoadSampler(
			{ filter, filter,
			  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			  VK_SAMPLER_MIPMAP_MODE_NEAREST });
		return resourceManager.LoadTexture(name,
			make_unique<Image>(device, desc), sampler);
	}
}

namespace Core
{
	TerrainQuadTree::TerrainQuadTree(Device& device, ResourceManager& resourceManager,
		SyncContext& syncContext, const TerrainConfig& config)
		: _config(config)
		, _slots(syncContext, config.atlasCapacity)
	{
		uint32_t rows = config.AtlasRows();
		_heightExtent = uvec2(config.atlasSlotsPerRow, rows) * config.HeightTexels();
		_colorExtent = uvec2(config.atlasSlotsPerRow, rows) * config.ColorTexels();
		_splatExtent = uvec2(config.atlasSlotsPerRow, rows) * config.SplatTexels();

		_heightAtlas = CreateAtlasTexture(device, resourceManager, HEIGHT_ATLAS,
			_heightExtent, config.heightFormat);
		_normalAtlas = CreateAtlasTexture(device, resourceManager, NORMAL_ATLAS,
			_colorExtent, config.normalFormat);
		_albedoAtlas = CreateAtlasTexture(device, resourceManager, ALBEDO_ATLAS,
			_colorExtent, config.albedoFormat);
		_splatAtlas = CreateAtlasTexture(device, resourceManager, SPLAT_ATLAS,
			_splatExtent, config.splatFormat, 1, VK_FILTER_NEAREST);

		AtlasLayoutJob layoutJob({ &_heightAtlas.Get(), &_normalAtlas.Get(),
			&_albedoAtlas.Get(), &_splatAtlas.Get() });
		syncContext.SubmitImmediate(layoutJob);

		// Every LOD must map to an exact mip extent of the index texture.
		assert(config.NodesPerSide(0) >= (1u << (config.lodCount - 1)));
	}

	uvec2 TerrainQuadTree::SlotCoord(uint16_t slot) const
	{
		return uvec2(slot % _config.atlasSlotsPerRow, slot / _config.atlasSlotsPerRow);
	}

	uvec2 TerrainQuadTree::HeightTexelOrigin(uint16_t slot) const
	{
		return SlotCoord(slot) * _config.HeightTexels();
	}

	uvec2 TerrainQuadTree::ColorTexelOrigin(uint16_t slot) const
	{
		return SlotCoord(slot) * _config.ColorTexels();
	}

	uvec2 TerrainQuadTree::SplatTexelOrigin(uint16_t slot) const
	{
		return SlotCoord(slot) * _config.SplatTexels();
	}
}
