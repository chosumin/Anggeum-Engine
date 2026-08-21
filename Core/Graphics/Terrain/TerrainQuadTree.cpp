#include "stdafx.h"
#include "TerrainQuadTree.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/FrameCounter.h"
#include "Graphics/Vulkans/Image.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Buffer.h"

namespace
{
	using namespace Core;

	Handle<Texture> CreateAtlasTexture(Device& device, const char* name,
		uvec2 extent, VkFormat format, uint32_t mipLevels = 1,
		// Integer formats (the quadtree index) cannot be linearly filtered.
		VkFilter filter = VK_FILTER_LINEAR)
	{
		ImageDesc desc{};
		desc.extent = { extent.x, extent.y };
		desc.format = format;
		desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		desc.mipLevels = mipLevels;

		auto& resourceManager = device.GetResourceManager();
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
	TerrainQuadTree::TerrainQuadTree(Device& device, const TerrainConfig& config)
		: _config(config)
	{
		uint32_t rows = config.AtlasRows();
		_heightExtent = uvec2(config.atlasSlotsPerRow, rows) * config.HeightTexels();
		_colorExtent = uvec2(config.atlasSlotsPerRow, rows) * config.ColorTexels();

		_heightAtlas = CreateAtlasTexture(device, HEIGHT_ATLAS,
			_heightExtent, config.heightFormat);
		_normalAtlas = CreateAtlasTexture(device, NORMAL_ATLAS,
			_colorExtent, config.normalFormat);
		_albedoAtlas = CreateAtlasTexture(device, ALBEDO_ATLAS,
			_colorExtent, config.albedoFormat);

		// One texel per quadtree node: mip m holds LOD m. The finest LOD side
		// must be divisible so every LOD maps to an exact mip extent.
		assert(config.NodesPerSide(0) >= (1u << (config.lodCount - 1)));
		_indexTexture = CreateAtlasTexture(device, QUADTREE_INDEX,
			uvec2(config.NodesPerSide(0)), VK_FORMAT_R16_UINT, config.lodCount,
			VK_FILTER_NEAREST);

		_nodeDescBuffer = device.GetResourceManager().LoadBuffer(
			{ config.atlasCapacity * sizeof(TerrainNodeDescGPU),
			  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			  MemoryType::DEVICE_LOCAL },
			NODE_DESC);

		_freeSlots.reserve(config.atlasCapacity);
		for (uint32_t slot = config.atlasCapacity; slot > 0; --slot)
			_freeSlots.push_back(uint16_t(slot - 1));
	}

	uint16_t TerrainQuadTree::AllocateSlot()
	{
		if (_freeSlots.empty())
		{
			// Reclaim retired slots that no in-flight frame can reference.
			while (!_retiredSlots.empty()
				&& FrameCounter::GetFrameNumber()
				>= _retiredSlots.front().second + MAX_FRAMES_IN_FLIGHT)
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
		_retiredSlots.emplace_back(slot, FrameCounter::GetFrameNumber());
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
