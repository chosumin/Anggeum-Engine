#pragma once
#include "TerrainConfig.h"

namespace Core
{
	struct TerrainNodeId
	{
		uint8_t lod = 0;
		uint16_t x = 0;
		uint16_t y = 0;

		bool operator==(const TerrainNodeId& other) const
		{
			return lod == other.lod && x == other.x && y == other.y;
		}
	};

	// Global linear index over all nodes, finest LOD first (row-major per LOD).
	inline uint32_t ToLinearIndex(const TerrainNodeId& id, const TerrainConfig& config)
	{
		return config.LevelOffset(id.lod)
			+ uint32_t(id.y) * config.NodesPerSide(id.lod) + id.x;
	}

	enum class TerrainNodeState : uint8_t
	{
		Unloaded,
		PendingUpload,
		Resident,
	};

	struct TerrainNodeRuntime
	{
		TerrainNodeState state = TerrainNodeState::Unloaded;
		uint16_t atlasSlot = TERRAIN_NODE_EMPTY;
	};
}

template <>
struct std::hash<Core::TerrainNodeId>
{
	size_t operator()(const Core::TerrainNodeId& id) const noexcept
	{
		return (size_t(id.lod) << 32) ^ (size_t(id.y) << 16) ^ id.x;
	}
};
