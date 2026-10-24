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

	// A level pad around a world XZ point, eased into the surrounding height
	// over falloff: the Sponza floor sits on it.
	struct TerrainFlatSpot
	{
		vec2 centerXZ{ 0.0f, 0.0f };
		float radius = 40.0f;
		float falloff = 20.0f;
		float height = 0.0f;

		float Apply(vec2 worldXZ, float terrainHeight) const
		{
			float t = glm::smoothstep(radius, radius + falloff,
				glm::length(worldXZ - centerXZ));
			return glm::mix(height, terrainHeight, t);
		}
	};

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
		// (ImageTerrainHeightSource hashes the file path and write time.)
		virtual uint32_t CacheKey() const = 0;
	};
}
