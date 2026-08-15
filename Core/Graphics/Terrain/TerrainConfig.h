#pragma once

namespace Core
{
	// World, bake, and streaming parameters for the terrain system.
	// The world is a grid of root quadtree tiles; each root subdivides into
	// lodCount levels. LOD 0 is the finest (a "sector"), lodCount-1 the root.
	struct TerrainConfig
	{
		// World
		uint32_t rootTilesX = 1;
		uint32_t rootTilesZ = 1;
		float rootNodeSize = 2048.0f;
		uint32_t lodCount = 6;
		uint32_t quadCountPerNodeEdge = 128;   // quads per node edge
		uint32_t borderTexels = 2;  // normal/albedo filtering apron
		// Chosen so the average terrain height lands near y = 0 (the Sponza
		// floor): the FBM mapping averages to the middle of the range.
		float heightMin = -100.0f;
		float heightMax = 100.0f;

		// Formats. Uncompressed for now: the engine has no BC encoder yet.
		VkFormat heightFormat = VK_FORMAT_R16_UNORM;
		VkFormat normalFormat = VK_FORMAT_R8G8B8A8_UNORM;
		VkFormat albedoFormat = VK_FORMAT_R8G8B8A8_UNORM;

		// Streaming. Capacity is intentionally smaller than the total node
		// count so slot pressure and parent fallback actually happen.
		uint32_t atlasCapacity = 512;
		uint32_t atlasSlotsPerRow = 32;
		float ringRadiusScale = 2.0f;   // load radius = scale * NodeSize(lod)
		float evictHysteresis = 1.15f;
		uint32_t uploadBudgetPerFrame = 8;

		uint32_t HeightTexels() const { return quadCountPerNodeEdge + 1; }
		uint32_t ColorTexels() const { return quadCountPerNodeEdge + 2 * borderTexels; }

		// The world is centered on the origin; the min corner follows from the
		// root tile grid so growing rootTilesX/Z cannot leave a stale origin.
		vec2 WorldOrigin() const
		{
			return vec2(-float(rootTilesX), -float(rootTilesZ)) * rootNodeSize * 0.5f;
		}

		// Depth below the root: 0 at the root LOD, lodCount-1 at LOD 0.
		uint32_t Depth(uint32_t lod) const { return lodCount - 1 - lod; }
		uint32_t NodesPerSide(uint32_t lod) const { return rootTilesX << Depth(lod); }
		float NodeSize(uint32_t lod) const
		{
			return rootNodeSize / float(1u << Depth(lod));
		}

		uint32_t NodeCount(uint32_t lod) const
		{
			uint32_t side = NodesPerSide(lod);
			return side * side;
		}

		uint32_t TotalNodeCount() const
		{
			uint32_t total = 0;
			for (uint32_t lod = 0; lod < lodCount; ++lod)
				total += NodeCount(lod);
			return total;
		}

		// Offset of a LOD's node range in the global linear node index.
		// Ordered finest-first: LOD 0 nodes, then LOD 1, ...
		uint32_t LevelOffset(uint32_t lod) const
		{
			uint32_t offset = 0;
			for (uint32_t l = 0; l < lod; ++l)
				offset += NodeCount(l);
			return offset;
		}

		uint32_t AtlasRows() const
		{
			return (atlasCapacity + atlasSlotsPerRow - 1) / atlasSlotsPerRow;
		}
	};

	// Sentinels for the quadtree index texture (R16_UINT, one texel per node).
	constexpr uint16_t TERRAIN_NODE_EMPTY = 0xffff;   // not resident
	constexpr uint16_t TERRAIN_NODE_INVALID = 0xfffe; // outside the world

	// GPU node description, one per atlas slot (slot index == desc index).
	// Mirrored in terrain shaders; consumed by the phase-2 GPU pipeline.
	struct TerrainNodeDescGPU
	{
		uint32_t minMaxHeight; // bits 0-15 min, 16-31 max (unorm16)
		uint32_t slotLod;      // bits 0-7 slotX, 8-15 slotY, 16-19 lod
	};

	// Per-drawn-node instance data read by terrain.vert.
	struct alignas(16) TerrainNodeInstance
	{
		vec2 originXZ{};            // world-space min corner
		float sizeMeters = 0.0f;
		uint32_t lod = 0;
		uvec2 heightTexelOrigin{};  // slot origin in the height atlas
		uvec2 colorTexelOrigin{};   // slot origin in the normal/albedo atlases
	};

	// Push-constant block of the GPU traversal shaders (node list, LOD map);
	struct TerrainTraversalPush
	{
		vec2 cameraXZ{};
		vec2 worldOrigin{};
		float rootNodeSize = 0.0f;
		float ringRadiusScale = 0.0f;
		uint32_t lodCount = 0;
		uint32_t rootTiles = 0;
	};

	// Shared uniform block for terrain.vert/.frag.
	struct alignas(16) TerrainParams
	{
		vec4 heightMinMaxInvAtlas{};  // x = min, y = max, zw = 1 / heightAtlasExtent
		vec4 invColorAtlasBorder{};   // xy = 1 / colorAtlasExtent, z = borderTexels
		vec4 sunDirection{};          // xyz = direction, w = ambient
		ivec4 debugMode{};            // x: 0 lit, 1 LOD tint, 2 normals, 3 uv grid
	};
}
