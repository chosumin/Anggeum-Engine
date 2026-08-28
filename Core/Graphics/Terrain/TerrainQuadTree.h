#pragma once
#include "TerrainConfig.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
	class Device;
	class ResourceManager;
	class Texture;
	class Buffer;

	// Owns the GPU face of the terrain world structure: the three payload
	// atlases (synchronized slots - one slot index addresses all of them),
	// the mip-mapped quadtree index texture, and the node description buffer.
	class TerrainQuadTree
	{
	public:
		static constexpr const char* HEIGHT_ATLAS = "Terrain.HeightAtlas";
		static constexpr const char* NORMAL_ATLAS = "Terrain.NormalAtlas";
		static constexpr const char* ALBEDO_ATLAS = "Terrain.AlbedoAtlas";
		static constexpr const char* QUADTREE_INDEX = "Terrain.QuadtreeIndex";
		static constexpr const char* NODE_DESC = "Terrain.NodeDesc";

		TerrainQuadTree(Device& device, ResourceManager& resourceManager, const TerrainConfig& config);

		Handle<Texture> GetHeightAtlas() const { return _heightAtlas; }
		Handle<Texture> GetNormalAtlas() const { return _normalAtlas; }
		Handle<Texture> GetAlbedoAtlas() const { return _albedoAtlas; }
		Handle<Texture> GetIndexTexture() const { return _indexTexture; }
		Handle<Buffer> GetNodeDescBuffer() const { return _nodeDescBuffer; }

		uvec2 GetHeightAtlasExtent() const { return _heightExtent; }
		uvec2 GetColorAtlasExtent() const { return _colorExtent; }

		// Returns TERRAIN_NODE_EMPTY when no slot can be handed out safely
		// this frame (all occupied or still retiring).
		uint16_t AllocateSlot();

		// Frame-stamped retire: a released slot may still be sampled by
		// in-flight frames, so it re-enters the free list only after
		// MAX_FRAMES_IN_FLIGHT frames.
		void ReleaseSlot(uint16_t slot);

		uvec2 HeightTexelOrigin(uint16_t slot) const;
		uvec2 ColorTexelOrigin(uint16_t slot) const;

		uint32_t GetFreeSlotCount() const
		{
			return uint32_t(_freeSlots.size() + _retiredSlots.size());
		}

	private:
		const TerrainConfig& _config;
		uvec2 _heightExtent{};
		uvec2 _colorExtent{};

		Handle<Texture> _heightAtlas;
		Handle<Texture> _normalAtlas;
		Handle<Texture> _albedoAtlas;
		Handle<Texture> _indexTexture;
		Handle<Buffer> _nodeDescBuffer;

		vector<uint16_t> _freeSlots;
		deque<pair<uint16_t, uint64_t>> _retiredSlots; // slot, release frame
	};
}
