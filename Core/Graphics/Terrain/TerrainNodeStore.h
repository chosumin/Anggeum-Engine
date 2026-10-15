#pragma once
#include "TerrainBaker.h"

namespace Core
{
	class TerrainHeightSource;

	class TerrainNodeStore
	{
	public:
		// The runtime entry point: loads the bake cache when it matches the
		// config, height source and palette far side, otherwise bakes and
		// saves the cache for the next run.
		static TerrainNodeStore Load(const TerrainConfig& config,
			const TerrainHeightSource& heightSource,
			const vector<TerrainMaterialBakeDesc>& materials);

		TerrainNodeStore(const TerrainConfig& config, vector<TerrainNodePayload>&& payloads)
			: _config(config), _payloads(move(payloads)) {}

		const TerrainNodePayload& Get(const TerrainNodeId& id) const
		{
			return _payloads[ToLinearIndex(id, _config)];
		}

	private:
		const TerrainConfig& _config;
		vector<TerrainNodePayload> _payloads;
	};
}
