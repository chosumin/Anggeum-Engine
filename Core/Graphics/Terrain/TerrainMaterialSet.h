#pragma once
#include "TerrainMaterial.h"

namespace Core
{
	class Job;
	class FrameResources;

	// The runtime material palette: the descriptors, the detail textures
	// behind them and the GPU palette they resolve to. 
	class TerrainMaterialSet
	{
	public:
		TerrainMaterialSet(vector<TerrainMaterialDesc>&& materials,
			vector<TerrainMaterialTextures>&& textures, vector<unique_ptr<Job>>&& initJobs);
		~TerrainMaterialSet();

		const vector<TerrainMaterialDesc>& GetDescs() const { return _materials; }

		// Rebuilt per frame by the renderer.
		TerrainMaterialPaletteGPU BuildPalette() const;

		// Queues the producer's fill jobs once.
		void QueuePendingInit(FrameResources& frameResources);

		void OnGUI();

	private:
		vector<TerrainMaterialDesc> _materials;
		vector<TerrainMaterialTextures> _textures;
		vector<unique_ptr<Job>> _initJobs;
	};
}
