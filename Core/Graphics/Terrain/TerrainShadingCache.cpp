#include "stdafx.h"
#include "TerrainShadingCache.h"
#include "TerrainNodeStore.h"
#include "TerrainStreamer.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/Vulkans/Image.h"
#include "Graphics/Vulkans/Texture.h"

namespace
{
	using namespace Core;

	Handle<Texture> CreateCacheTexture(Device& device, ResourceManager& resourceManager,
		const char* name, uint32_t extent, Handle<Sampler> sampler)
	{
		ImageDesc desc{};
		desc.extent = { extent, extent };
		desc.format = VK_FORMAT_R8G8B8A8_UNORM;
		desc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		return resourceManager.LoadTexture(name, make_unique<Image>(device, desc), sampler);
	}
}

namespace Core
{
	TerrainShadingCache::TerrainShadingCache(Device& device, ResourceManager& resourceManager,
		SyncContext& syncContext, const TerrainConfig& config,
		const TerrainNodeStore& store, const TerrainStreamer& streamer)
		: _config(config)
		, _cache(config.shadingCache)
		, _store(store)
		, _streamer(streamer)
		, _slots(syncContext, config.shadingCache.SlotCount())
	{
		assert(_cache.levelCount >= 1 && _cache.levelCount <= TERRAIN_CACHE_MAX_LEVELS);
		assert(_cache.SlotCount() < TERRAIN_PAGE_EMPTY);
		// Every level must tile the world exactly, so page coordinates stay integral.
		float worldSize = float(config.rootTilesX) * config.rootNodeSize;
		assert(fmod(worldSize, _cache.PageSize(_cache.levelCount - 1)) == 0.0f);
		(void)worldSize;

		// Bilinear within a page; the apron makes it seamless across pages.
		Handle<Sampler> sampler = resourceManager.LoadSampler(
			{ VK_FILTER_LINEAR, VK_FILTER_LINEAR,
			  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			  VK_SAMPLER_MIPMAP_MODE_NEAREST });
		_albedo = CreateCacheTexture(device, resourceManager, ALBEDO_CACHE, _cache.Extent(), sampler);
		_normal = CreateCacheTexture(device, resourceManager, NORMAL_CACHE, _cache.Extent(), sampler);

		_pages.resize(_cache.SlotCount());
		_table.assign(TERRAIN_CACHE_MAX_LEVELS * TERRAIN_CACHE_WINDOW * TERRAIN_CACHE_WINDOW,
			TERRAIN_PAGE_EMPTY);
	}

	uint32_t TerrainShadingCache::PagesPerSide(uint32_t level) const
	{
		float worldSize = float(_config.rootTilesX) * _config.rootNodeSize;
		return uint32_t(glm::round(worldSize / _cache.PageSize(level)));
	}

	float TerrainShadingCache::RequestRadius(uint32_t level) const
	{
		return _settings.ringScale * _cache.PageSize(level);
	}

	bool TerrainShadingCache::InWindow(const PageId& page) const
	{
		ivec2 rel = ivec2(page.x, page.y) - _windowOrigin[page.level];
		return rel.x >= 0 && rel.y >= 0
			&& rel.x < int(TERRAIN_CACHE_WINDOW) && rel.y < int(TERRAIN_CACHE_WINDOW);
	}

	uint32_t TerrainShadingCache::TableIndex(const PageId& page) const
	{
		const int32_t W = int32_t(TERRAIN_CACHE_WINDOW);
		int32_t wx = ((page.x % W) + W) % W;
		int32_t wy = ((page.y % W) + W) % W;
		return uint32_t(page.level) * W * W + uint32_t(wy * W + wx);
	}

	float TerrainShadingCache::Distance(vec3 cameraPos, const PageId& page) const
	{
		float size = _cache.PageSize(page.level);
		vec2 pageMin = _config.WorldOrigin() + vec2(float(page.x), float(page.y)) * size;
		vec2 cameraXZ(cameraPos.x, cameraPos.z);
		vec2 closest = clamp(cameraXZ, pageMin, pageMin + size);
		float dxz = length(cameraXZ - closest);

		// The smallest node at least as large as the page bounds its heights.
		uint32_t lod = 0;
		while (lod + 1 < _config.lodCount && _config.NodeSize(lod) < size)
			++lod;
		float nodeSize = _config.NodeSize(lod);
		int32_t side = int32_t(_config.NodesPerSide(lod));
		ivec2 node = clamp(ivec2(glm::floor((pageMin + size * 0.5f - _config.WorldOrigin()) / nodeSize)),
			ivec2(0), ivec2(side - 1));
		const TerrainNodePayload& payload = _store.Get({ uint8_t(lod), uint16_t(node.x), uint16_t(node.y) });
		float minHeight = glm::mix(_config.heightMin, _config.heightMax, float(payload.minHeight) / 65535.0f);
		float maxHeight = glm::mix(_config.heightMin, _config.heightMax, float(payload.maxHeight) / 65535.0f);
		float dy = cameraPos.y < minHeight ? minHeight - cameraPos.y
			: cameraPos.y > maxHeight ? cameraPos.y - maxHeight : 0.0f;
		return sqrt(dxz * dxz + dy * dy);
	}

	uint8_t TerrainShadingCache::FinestResidentLod(const PageId& page) const
	{
		float size = _cache.PageSize(page.level);
		vec2 center = vec2(float(page.x) + 0.5f, float(page.y) + 0.5f) * size;
		ivec2 sector = ivec2(glm::floor(center / _config.NodeSize(0)));
		for (uint32_t lod = 0; lod < _config.lodCount; ++lod)
		{
			ivec2 node = sector >> int(lod);
			if (_streamer.IsResident({ uint8_t(lod), uint16_t(node.x), uint16_t(node.y) }))
				return uint8_t(lod);
		}
		return uint8_t(_config.lodCount - 1); // the root is always resident
	}

	void TerrainShadingCache::Register(uint16_t slot, const PageId& page, uint8_t sourceLod)
	{
		_pages[slot] = { true, page, sourceLod };
		_table[TableIndex(page)] = slot;
	}

	void TerrainShadingCache::Evict(uint16_t slot)
	{
		_table[TableIndex(_pages[slot].page)] = TERRAIN_PAGE_EMPTY;
		_pages[slot] = {};
		_slots.Release(slot);
	}

	TerrainPageRenderGPU TerrainShadingCache::MakeRender(const PageId& page, uint16_t slot) const
	{
		TerrainPageRenderGPU render;
		render.size = _cache.PageSize(page.level);
		render.originXZ = _config.WorldOrigin() + vec2(float(page.x), float(page.y)) * render.size;
		render.slot = slot;
		return render;
	}

	void TerrainShadingCache::Update(vec3 cameraPos)
	{
		_stats = {};
		_renderList.clear();
		if (!_settings.enabled)
			return; // pages stay for re-enabling; the window diff below cleans up then

		const int32_t W = int32_t(TERRAIN_CACHE_WINDOW);
		vec2 cameraXZ(cameraPos.x, cameraPos.z);
		for (uint32_t level = 0; level < _cache.levelCount; ++level)
		{
			ivec2 cameraPage = ivec2(glm::floor((cameraXZ - _config.WorldOrigin()) / _cache.PageSize(level)));
			_windowOrigin[level] = cameraPage - ivec2(W / 2);
		}

		// Pass 1: evict pages that left the window or the hysteresis radius.
		// Everything left is inside the window, so toroidal indices are unique.
		for (uint16_t slot = 0; slot < _pages.size(); ++slot)
		{
			const PageSlot& page = _pages[slot];
			if (!page.used)
				continue;

			float evictRadius = RequestRadius(page.page.level) * _settings.evictHysteresis;
			if (!InWindow(page.page) || Distance(cameraPos, page.page) > evictRadius)
			{
				Evict(slot);
				++_stats.evictedThisFrame;
			}
		}

		// Pass 2: diff the ring against the table. A resident page rendered
		// from a coarser node than now resident is stale (coarser splat and
		// normals) and refreshes in place.
		vector<PageRequest> toLoad, toRefresh;
		for (uint32_t level = 0; level < _cache.levelCount; ++level)
		{
			int32_t side = int32_t(PagesPerSide(level));
			float radius = RequestRadius(level);
			for (int32_t wy = 0; wy < W; ++wy)
				for (int32_t wx = 0; wx < W; ++wx)
				{
					PageId page{ uint8_t(level), _windowOrigin[level].x + wx, _windowOrigin[level].y + wy };
					if (page.x < 0 || page.y < 0 || page.x >= side || page.y >= side)
						continue;

					float distance = Distance(cameraPos, page);
					if (distance > radius)
						continue;
					++_stats.requested;

					uint16_t slot = _table[TableIndex(page)];
					if (slot == TERRAIN_PAGE_EMPTY)
					{
						toLoad.push_back({ page, distance });
						continue;
					}

					uint8_t finest = FinestResidentLod(page);
					if (finest < _pages[slot].sourceLod)
						toRefresh.push_back({ page, distance, finest, slot });
				}
		}

		// Pass 3: budgeted renders, coarse levels first (they cover the gaps
		// while finer pages arrive), then near-to-far; refreshes last.
		sort(toLoad.begin(), toLoad.end(),
			[](const PageRequest& a, const PageRequest& b)
			{
				if (a.page.level != b.page.level)
					return a.page.level > b.page.level;
				return a.distance < b.distance;
			});
		sort(toRefresh.begin(), toRefresh.end(),
			[](const PageRequest& a, const PageRequest& b) { return a.distance < b.distance; });

		uint32_t budget = std::min<uint32_t>(uint32_t(std::max(_settings.renderBudgetPerFrame, 0)),
			TERRAIN_CACHE_MAX_PAGE_RENDERS);
		for (const PageRequest& request : toLoad)
		{
			if (_renderList.size() >= budget)
			{
				++_stats.pending;
				continue;
			}

			uint16_t slot = _slots.Allocate();
			if (slot == TERRAIN_NODE_EMPTY)
			{
				++_stats.starved;
				continue;
			}

			Register(slot, request.page, FinestResidentLod(request.page));
			_renderList.push_back(MakeRender(request.page, slot));
			++_stats.renderedThisFrame;
		}
		for (const PageRequest& request : toRefresh)
		{
			if (_renderList.size() >= budget)
			{
				++_stats.pending;
				continue;
			}

			_pages[request.slot].sourceLod = request.sourceLod;
			_renderList.push_back(MakeRender(request.page, request.slot));
			++_stats.refreshedThisFrame;
		}

		for (const PageSlot& page : _pages)
			if (page.used)
				++_stats.resident;
	}

	TerrainPageTableGPU TerrainShadingCache::BuildPageTable() const
	{
		TerrainPageTableGPU table{};
		for (uint32_t level = 0; level < TERRAIN_CACHE_MAX_LEVELS; ++level)
			table.windowOrigin[level] = ivec4(_windowOrigin[level], 0, 0);
		for (uint32_t i = 0; i < _table.size(); ++i)
			table.entries[i / 4][i % 4] = _table[i];
		return table;
	}

	void TerrainShadingCache::FillParams(TerrainParams& params) const
	{
		params.cacheInfo = vec4(_settings.enabled ? 1.0f : 0.0f, float(_cache.pageTexels),
			float(_cache.borderTexels), float(_cache.slotsPerRow));
		params.cacheLevels = vec4(_cache.finestPageSize, float(_cache.levelCount),
			1.0f / float(_cache.Extent()), _settings.trilinear ? 1.0f : 0.0f);
	}

	void TerrainShadingCache::OnGUI()
	{
		ImGui::Checkbox("Enabled", &_settings.enabled);
		ImGui::SetItemTooltip("Off: every near pixel runs the material blend itself");
		ImGui::Text("Pages: %u requested, %u resident, %u pending, %u starved",
			_stats.requested, _stats.resident, _stats.pending, _stats.starved);
		ImGui::Text("This frame: %u rendered, %u refreshed, %u evicted, %u free slots",
			_stats.renderedThisFrame, _stats.refreshedThisFrame, _stats.evictedThisFrame,
			_slots.GetFreeCount());
		ImGui::Text("Cache: %u^2 texels x 2, %u slots of %u^2 + %u apron; pages %.1f .. %.0f m (%.1f cm texels at level 0)",
			_cache.Extent(), _cache.SlotCount(), _cache.pageTexels, _cache.borderTexels,
			_cache.PageSize(0), _cache.PageSize(_cache.levelCount - 1),
			100.0f * _cache.PageSize(0) / float(_cache.pageTexels));

		// The window bounds the ring: scale * hysteresis + 1 pages must fit in half of it.
		float maxScale = float(TERRAIN_CACHE_WINDOW / 2 - 1) / _settings.evictHysteresis;
		ImGui::SliderFloat("Ring scale", &_settings.ringScale, 0.5f, maxScale, "%.2f x page size");
		ImGui::SliderFloat("Evict hysteresis", &_settings.evictHysteresis, 1.0f, 1.5f, "%.2f");
		_settings.ringScale = std::min(_settings.ringScale, maxScale);
		ImGui::SliderInt("Render budget", &_settings.renderBudgetPerFrame, 0, int(TERRAIN_CACHE_MAX_PAGE_RENDERS), "%d pages / frame");
		ImGui::Checkbox("Trilinear (blend the next coarser level)", &_settings.trilinear);
	}
}
