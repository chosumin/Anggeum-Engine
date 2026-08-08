#pragma once
#include "TerrainNode.h"

namespace Core
{
	class TerrainHeightSource;

	// Baked CPU payload for one quadtree node. This is the in-memory stand-in
	// for FC5's on-disk tile: streaming copies from here into the GPU atlases.
	struct TerrainNodePayload
	{
		vector<uint16_t> height;  // HeightTexels()^2, unorm16
		vector<uint32_t> normal;  // ColorTexels()^2, RGBA8 packed (n * 0.5 + 0.5)
		vector<uint32_t> albedo;  // ColorTexels()^2, RGBA8
		uint16_t minHeight = 0;
		uint16_t maxHeight = 0;
	};

	class TerrainNodeStore
	{
	public:
		// The runtime entry point: loads the bake cache when it matches the
		// config and height source, otherwise bakes and saves
		// the cache for the next run.
		static TerrainNodeStore Load(const TerrainConfig& config,
			const TerrainHeightSource& heightSource);

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
