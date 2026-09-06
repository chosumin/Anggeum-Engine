#include "stdafx.h"
#include "TerrainQuadTree.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/SyncContext.h"
#include "Graphics/Vulkans/Image.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Buffer.h"

namespace
{
	using namespace Core;

	Handle<Texture> CreateAtlasTexture(Device& device,
		ResourceManager& resourceManager, const char* name,
		uvec2 extent, VkFormat format, uint32_t mipLevels = 1,
		// Integer formats (the quadtree index) cannot be linearly filtered.
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
		, _sync(syncContext)
	{
		uint32_t rows = config.AtlasRows();
		_heightExtent = uvec2(config.atlasSlotsPerRow, rows) * config.HeightTexels();
		_colorExtent = uvec2(config.atlasSlotsPerRow, rows) * config.ColorTexels();

		_heightAtlas = CreateAtlasTexture(device, resourceManager, HEIGHT_ATLAS,
			_heightExtent, config.heightFormat);
		_normalAtlas = CreateAtlasTexture(device, resourceManager, NORMAL_ATLAS,
			_colorExtent, config.normalFormat);
		_albedoAtlas = CreateAtlasTexture(device, resourceManager, ALBEDO_ATLAS,
			_colorExtent, config.albedoFormat);

		// Every LOD must map to an exact mip extent of the index texture.
		assert(config.NodesPerSide(0) >= (1u << (config.lodCount - 1)));

		_freeSlots.reserve(config.atlasCapacity);
		for (uint32_t slot = config.atlasCapacity; slot > 0; --slot)
			_freeSlots.push_back(uint16_t(slot - 1));
	}

	uint16_t TerrainQuadTree::AllocateSlot()
	{
		if (_freeSlots.empty())
		{
			// Reclaim retired slots once the GPU passed their last possible
			// reader. The per-frame cache is enough: stale merely delays.
			const u64 completed = _sync.GetCompletedValue(QueueType::Graphics);
			while (!_retiredSlots.empty() && completed >= _retiredSlots.front().second)
			{
				_freeSlots.push_back(_retiredSlots.front().first);
				_retiredSlots.pop_front();
			}
		}

		if (_freeSlots.empty())
			return TERRAIN_NODE_EMPTY;

		uint16_t slot = _freeSlots.back();
		_freeSlots.pop_back();
		return slot;
	}

	void TerrainQuadTree::ReleaseSlot(uint16_t slot)
	{
		assert(slot < _config.atlasCapacity);

		// The current graphics value bounds every frame submitted so far -
		// and this frame's draws use the updated tables (the eviction's
		// table job lands this frame), so no later frame samples the slot.
		_retiredSlots.emplace_back(slot, _sync.GetCurrentValue(QueueType::Graphics));
	}

	uvec2 TerrainQuadTree::HeightTexelOrigin(uint16_t slot) const
	{
		return uvec2(slot % _config.atlasSlotsPerRow,
			slot / _config.atlasSlotsPerRow) * _config.HeightTexels();
	}

	uvec2 TerrainQuadTree::ColorTexelOrigin(uint16_t slot) const
	{
		return uvec2(slot % _config.atlasSlotsPerRow,
			slot / _config.atlasSlotsPerRow) * _config.ColorTexels();
	}
}
