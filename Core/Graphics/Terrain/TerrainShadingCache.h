#pragma once
#include "TerrainConfig.h"
#include "TerrainSlotAllocator.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
	class Device;
	class ResourceManager;
	class SyncContext;
	class Texture;
	class TerrainNodeStore;
	class TerrainStreamer;

	struct TerrainShadingCacheStats
	{
		uint32_t requested = 0;
		uint32_t resident = 0;
		uint32_t pending = 0;   // requested, not rendered this frame (budget)
		uint32_t starved = 0;   // requested but no cache slot available
		uint32_t renderedThisFrame = 0;
		uint32_t refreshedThisFrame = 0;
		uint32_t evictedThisFrame = 0;
	};

	// The shading cache: a page tree over the world (finest pages nearest the
	// camera, each level doubling) whose resident pages hold the near
	// material blend, rendered once by terrainPageRender.comp into two
	// physical textures. The CPU decides residency per level with the same
	// ring + hysteresis scheme as tile streaming, keeps the per-level page
	// table windows the fragment shader resolves pages through, and hands
	// the frame its page render list. A page rendered from a coarser node
	// than now resident is re-rendered in place.
	class TerrainShadingCache
	{
	public:
		static constexpr const char* ALBEDO_CACHE = "Terrain.ShadingCacheAlbedo";
		static constexpr const char* NORMAL_CACHE = "Terrain.ShadingCacheNormal";
		static constexpr const char* UB_PAGE_TABLE = "Terrain.PageTable";
		static constexpr const char* UB_PAGE_RENDER_LIST = "Terrain.PageRenderList";

		TerrainShadingCache(Device& device, ResourceManager& resourceManager,
			SyncContext& syncContext, const TerrainConfig& config,
			const TerrainNodeStore& store, const TerrainStreamer& streamer);

		// Main thread, once per frame, after the streamer's update: page
		// sources are the nodes resident this frame.
		void Update(vec3 cameraPos);

		bool IsEnabled() const { return _settings.enabled; }

		// Pages to render this frame (TerrainPageRenderPass).
		const vector<TerrainPageRenderGPU>& GetRenderList() const { return _renderList; }

		// The page table the terrain draws resolve through this frame.
		TerrainPageTableGPU BuildPageTable() const;

		void FillParams(TerrainParams& params) const;

		Handle<Texture> GetAlbedoCache() const { return _albedo; }
		Handle<Texture> GetNormalCache() const { return _normal; }

		const TerrainShadingCacheStats& GetStats() const { return _stats; }
		void OnGUI();

	private:
		struct PageId
		{
			uint8_t level = 0;
			int32_t x = 0;
			int32_t y = 0;
		};

		struct PageSlot
		{
			bool used = false;
			PageId page;
			uint8_t sourceLod = 0; // the finest resident node LOD it was rendered from
		};

		struct PageRequest
		{
			PageId page;
			float distance = 0.0f;
			uint8_t sourceLod = 0;
			uint16_t slot = TERRAIN_PAGE_EMPTY; // refresh: the slot it already holds
		};

		uint32_t PagesPerSide(uint32_t level) const;
		float RequestRadius(uint32_t level) const;
		bool InWindow(const PageId& page) const;
		// Toroidal table index: absolute page coordinate modulo the window.
		uint32_t TableIndex(const PageId& page) const;
		// Camera to the page's box: XZ to its square, Y to the height range
		// of the node of matching size under its centre.
		float Distance(vec3 cameraPos, const PageId& page) const;
		uint8_t FinestResidentLod(const PageId& page) const;
		void Register(uint16_t slot, const PageId& page, uint8_t sourceLod);
		void Evict(uint16_t slot);
		TerrainPageRenderGPU MakeRender(const PageId& page, uint16_t slot) const;

		const TerrainConfig& _config;
		const TerrainShadingCacheConfig& _cache;
		const TerrainNodeStore& _store;
		const TerrainStreamer& _streamer;

		Handle<Texture> _albedo;
		Handle<Texture> _normal;

		TerrainSlotAllocator _slots;
		vector<PageSlot> _pages;                         // by cache slot
		vector<uint16_t> _table;                         // level x window x window, toroidal
		array<ivec2, TERRAIN_CACHE_MAX_LEVELS> _windowOrigin{};
		vector<TerrainPageRenderGPU> _renderList;

		TerrainShadingCacheSettings _settings;
		TerrainShadingCacheStats _stats;
	};
}
