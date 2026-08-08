#pragma once

namespace Core
{
	// FNV-1a, chainable. Used to build bake-cache keys from parameter fields.
	inline uint32_t TerrainHashBytes(const void* data, size_t size,
		uint32_t hash = 2166136261u)
	{
		const uint8_t* bytes = static_cast<const uint8_t*>(data);
		for (size_t i = 0; i < size; ++i)
		{
			hash ^= bytes[i];
			hash *= 16777619u;
		}
		return hash;
	}

	// Where terrain height comes from.
	class TerrainHeightSource
	{
	public:
		virtual ~TerrainHeightSource() = default;

		// Height in meters at a world XZ position. Must be defined slightly
		// outside the world bounds too: apron texels sample past node edges.
		virtual float Sample(vec2 worldXZ) const = 0;

		// Identity of the produced heights for bake-cache invalidation: two
		// sources with the same key must produce the same Sample() results.
		// (An image-based source would hash the file path and write time.)
		virtual uint32_t CacheKey() const = 0;
	};
}
