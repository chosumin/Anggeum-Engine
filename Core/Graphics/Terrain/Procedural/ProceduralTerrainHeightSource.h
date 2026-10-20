#pragma once
#include "TerrainNoise.h"
#include "Graphics/Terrain/TerrainHeightSource.h"

namespace Core
{
	// A level pad around a world XZ point, eased into the noise over falloff.
	struct TerrainFlatSpot
	{
		vec2 centerXZ{ 0.0f, 0.0f };
		float radius = 40.0f;
		float falloff = 20.0f;
		float height = 0.0f;
	};

	class ProceduralTerrainHeightSource : public TerrainHeightSource
	{
	public:
		ProceduralTerrainHeightSource(const TerrainNoiseParams& params,
			float heightMin, float heightMax, const TerrainFlatSpot& flat = {})
			: _params(params), _heightMin(heightMin), _heightMax(heightMax), _flat(flat) {}

		float Sample(vec2 worldXZ) const override
		{
			float n = TerrainNoise::Fbm(worldXZ, _params);
			// Map [-1, 1] into the encodable range with headroom so extreme
			// noise never clips against the unorm16 packing.
			float range = _heightMax - _heightMin;
			float height = _heightMin + (n * 0.5f + 0.5f) * range * 0.85f
				+ range * 0.075f;
			float t = glm::smoothstep(_flat.radius, _flat.radius + _flat.falloff,
				glm::length(worldXZ - _flat.centerXZ));
			return glm::mix(_flat.height, height, t);
		}

		uint32_t CacheKey() const override
		{
			// TerrainNoiseParams is 4-byte fields only, so no padding bytes.
			static_assert(sizeof(TerrainNoiseParams) == 6 * 4,
				"update CacheKey when TerrainNoiseParams changes");
			uint32_t hash = TerrainHashBytes(&_params, sizeof(_params));
			hash = TerrainHashBytes(&_heightMin, sizeof(_heightMin), hash);
			hash = TerrainHashBytes(&_heightMax, sizeof(_heightMax), hash);
			float flat[5] = { _flat.centerXZ.x, _flat.centerXZ.y, _flat.radius, _flat.falloff, _flat.height };
			return TerrainHashBytes(flat, sizeof(flat), hash); // explicit floats: no vec2 padding
		}

	private:
		TerrainNoiseParams _params;
		float _heightMin;
		float _heightMax;
		TerrainFlatSpot _flat;
	};
}
