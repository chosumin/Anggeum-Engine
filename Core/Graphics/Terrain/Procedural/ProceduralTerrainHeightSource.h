#pragma once
#include "TerrainNoise.h"
#include "Graphics/Terrain/TerrainHeightSource.h"

namespace Core
{
	class ProceduralTerrainHeightSource : public TerrainHeightSource
	{
	public:
		ProceduralTerrainHeightSource(const TerrainNoiseParams& params,
			float heightMin, float heightMax)
			: _params(params), _heightMin(heightMin), _heightMax(heightMax) {}

		float Sample(vec2 worldXZ) const override
		{
			float n = TerrainNoise::Fbm(worldXZ, _params);
			// Map [-1, 1] into the encodable range with headroom so extreme
			// noise never clips against the unorm16 packing.
			float range = _heightMax - _heightMin;
			return _heightMin + (n * 0.5f + 0.5f) * range * 0.85f
				+ range * 0.075f;
		}

		uint32_t CacheKey() const override
		{
			// TerrainNoiseParams is 4-byte fields only, so no padding bytes.
			static_assert(sizeof(TerrainNoiseParams) == 6 * 4,
				"update CacheKey when TerrainNoiseParams changes");
			uint32_t hash = TerrainHashBytes(&_params, sizeof(_params));
			hash = TerrainHashBytes(&_heightMin, sizeof(_heightMin), hash);
			return TerrainHashBytes(&_heightMax, sizeof(_heightMax), hash);
		}

	private:
		TerrainNoiseParams _params;
		float _heightMin;
		float _heightMax;
	};
}
