#pragma once
#include "Graphics/ResourceHandle.h"

namespace Core
{
	class Texture;

	// One entry of the runtime material palette
	struct TerrainMaterialDesc
	{
		const char* name = "";

		// World-space detail tiling
		float tileSize = 4.0f;      // meters per detail tile
		float rotationDegrees = 0.0f;
	};

	// The detail maps behind one palette entry
	struct TerrainMaterialTextures
	{
		Handle<Texture> albedo, normal, height;
	};

	enum TerrainMaterialId : uint8_t
	{
		TERRAIN_MATERIAL_GRASS = 0,
		TERRAIN_MATERIAL_DIRT = 1,
		TERRAIN_MATERIAL_ROCK = 2,
		TERRAIN_MATERIAL_SNOW = 3,
		TERRAIN_MATERIAL_COUNT = 4,
	};

	// Palette capacity on the GPU; the splat ID is 8-bit, with bit 7 reserved
	// for the cliff flag (Phase 4).
	constexpr uint32_t TERRAIN_MAX_MATERIALS = 16;
	constexpr uint8_t TERRAIN_SPLAT_ID_MASK = 0x7f;

	// Mirrored in terrainCommon.glsl (TerrainMaterial).
	struct alignas(16) TerrainMaterialGPU
	{
		uvec4 textures{}; // x albedo, y normal + smoothness, z height (bindless indices)
		vec4 tiling{};    // x = 1 / tileSize, y = cos(rotation), z = sin(rotation)
	};

	struct alignas(16) TerrainMaterialPaletteGPU
	{
		TerrainMaterialGPU materials[TERRAIN_MAX_MATERIALS];
		uvec4 count{}; // x = material count
	};
}
