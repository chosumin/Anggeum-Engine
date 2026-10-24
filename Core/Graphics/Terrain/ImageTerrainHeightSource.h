#pragma once
#include "TerrainHeightSource.h"
#include "TerrainConfig.h"
#include "Procedural/TerrainNoise.h"

namespace Core
{
	// Heights from a real-world DEM stretched over the whole world. The config
	// names a JSON descriptor next to a raw little-endian R16 file (row-major,
	// 0..65535 = heightMin..heightMax, column 0 at the world's min X, row 0 at
	// its min Z, so a north-up GeoTIFF needs no flip when -Z is north):
	//   { "data": "name.r16", "width": 4097, "height": 4097,
	//     "heightMin": -250.0, "heightMax": 410.0 }
	// Texel (0, 0) sits on the world's min corner and the last texel on the
	// max corner, so (quads per world edge + 1) texels put every LOD 0 vertex
	// exactly on one. Samples are bilinear and clamp at the border, which
	// keeps the baker's apron (outside the node edges) valid.
	// Tools/ConvertTerrainDem.py writes the pair from a GeoTIFF.
	class ImageTerrainHeightSource : public TerrainHeightSource
	{
	public:
		// The descriptor's range, so the config's heightMin/heightMax match
		// the file before anything hashes or packs with them. False (range
		// untouched) when the descriptor or its data file is unusable.
		static bool ReadHeightRange(const string& descriptorPath, float& heightMin, float& heightMax);

		// nullptr when the descriptor or its data file cannot be read.
		static unique_ptr<ImageTerrainHeightSource> Load(const TerrainConfig& config,
			const TerrainFlatSpot& flat = {});

		float Sample(vec2 worldXZ) const override;
		uint32_t CacheKey() const override;

	private:
		ImageTerrainHeightSource(const TerrainConfig& config, const TerrainFlatSpot& flat,
			vector<uint16_t>&& texels, uvec2 size, uint32_t fileKey);

		vector<uint16_t> _texels;
		uvec2 _size;
		vec2 _worldOrigin;
		vec2 _worldExtent;
		float _heightMin;
		float _heightMax;
		float _detailAmplitude;
		TerrainNoiseParams _detailParams;
		TerrainFlatSpot _flat;
		uint32_t _fileKey; // path + write times, folded into CacheKey
	};
}
