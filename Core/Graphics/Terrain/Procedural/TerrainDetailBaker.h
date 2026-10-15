#pragma once
#include "../TerrainBaker.h"

namespace Core
{
	// A full mip chain of one square map; mip m is (size >> m)^2 texels.
	struct TerrainDetailMipChain
	{
		uint32_t bytesPerTexel = 0;
		vector<vector<uint8_t>> mips;
	};

	// The three detail maps of one material, tileable in [0, 1).
	struct TerrainDetailMaps
	{
		TerrainDetailMipChain albedo;  // RGBA8
		TerrainDetailMipChain normal;  // RGBA8: tangent-space n * 0.5 + 0.5, a = smoothness
		TerrainDetailMipChain height;  // R8: relief, for the height-weighted blend
	};

	// A palette entry, its far side, and the look of its procedural detail maps.
	struct TerrainProceduralMaterial
	{
		TerrainMaterialDesc material;
		TerrainMaterialBakeDesc bake;
		vec3 albedoVariation{ 0.5f }; // second tint the detail noise leans toward
		float reliefScale = 0.4f;     // height amplitude in tile units, for the detail normal
		float frequency = 6.0f;       // noise cells per tile
		uint32_t seed = 1;
	};

	// Procedural stand-in for authored detail textures: tileable noise
	// shaped by the material's parameters. The albedo averages to the bake
	// albedo so the near/far transition holds.
	class TerrainDetailBaker
	{
	public:
		static uint32_t MipCount(uint32_t size);
		static TerrainDetailMaps Bake(const TerrainProceduralMaterial& material, uint32_t size);
	};
}
