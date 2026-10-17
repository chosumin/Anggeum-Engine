#pragma once
#include "TerrainDetailBaker.h"

namespace Core
{
	class Device;
	class ResourceManager;
	class BindlessTextureManager;
	class TerrainMaterialSet;

	// The procedural biome palette, indexed by TerrainMaterialId.
	vector<TerrainProceduralMaterial> ProceduralTerrainMaterialPalette();

	// The palette's far side, in the shape the node bake takes.
	vector<TerrainMaterialBakeDesc> ProceduralTerrainMaterialBakeDescs();

	// Bakes the palette's detail maps and hands a material set the results:
	// the procedural stand-in for loading authored material sets.
	unique_ptr<TerrainMaterialSet> CreateProceduralTerrainMaterials(Device& device,
		ResourceManager& resourceManager, BindlessTextureManager* bindless);
}
