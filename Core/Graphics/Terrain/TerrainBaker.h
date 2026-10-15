#pragma once
#include "TerrainNode.h"
#include "TerrainMaterial.h"

namespace Core
{
	class TerrainHeightSource;

	// Baked CPU payload for one quadtree node. This is the in-memory stand-in
	// for FC5's on-disk tile: streaming copies from here into the GPU atlases.
	struct TerrainNodePayload
	{
		vector<uint16_t> height;  // HeightTexels()^2, unorm16
		vector<uint32_t> normal;  // ColorTexels()^2, RGBA8: n.xz * 0.5 + 0.5, roughness, horizon AO
		vector<uint32_t> albedo;  // ColorTexels()^2, RGBA8
		vector<uint8_t> splat;    // SplatTexels()^2, material ID (TerrainMaterialId)
		uint16_t minHeight = 0;
		uint16_t maxHeight = 0;
	};

	// The far side of one material, indexed by TerrainMaterialId: what the
	// atlas albedo/roughness average to under the biome weights. Near detail
	// must average to the same, or the near/far transition shows.
	struct TerrainMaterialBakeDesc
	{
		vec3 albedo{ 0.5f };
		float roughness = 0.9f;
	};

	// Pure bake: a height source + the palette's far side -> per-node
	// height/normal/albedo/splat payloads.
	// No IO here — caching and loading live in TerrainNodeStore::Load, and
	// this eventually moves offline as a build/tool step.
	class TerrainBaker
	{
	public:
		static vector<TerrainNodePayload> Bake(const TerrainConfig& config,
			const TerrainHeightSource& heightSource,
			const vector<TerrainMaterialBakeDesc>& materials);
	};
}
