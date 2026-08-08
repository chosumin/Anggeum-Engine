#pragma once
#include "TerrainNodeStore.h"

namespace Core
{
	class TerrainHeightSource;

	// Pure bake: a height source -> per-node height/normal/albedo payloads.
	// No IO here — caching and loading live in TerrainNodeStore::Load, and
	// this eventually moves offline as a build/tool step.
	class TerrainBaker
	{
	public:
		static vector<TerrainNodePayload> Bake(const TerrainConfig& config,
			const TerrainHeightSource& heightSource);
	};
}
