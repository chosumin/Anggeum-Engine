#pragma once
#include "TerrainConfig.h"
#include "TerrainSlotAllocator.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
	class Device;
	class ResourceManager;
	class SyncContext;
	class Texture;
	class Buffer;

	// The four terrain payload atlases (one slot index addresses all) and
	// the slot lifetime: free list + frame-stamped retire.
	class TerrainQuadTree
	{
	public:
		static constexpr const char* HEIGHT_ATLAS = "Terrain.HeightAtlas";
		static constexpr const char* NORMAL_ATLAS = "Terrain.NormalAtlas";
		static constexpr const char* ALBEDO_ATLAS = "Terrain.AlbedoAtlas";
		static constexpr const char* SPLAT_ATLAS = "Terrain.SplatAtlas";
		static constexpr const char* QUADTREE_INDEX = "Terrain.QuadtreeIndex";
		static constexpr const char* NODE_DESC = "Terrain.NodeDesc";

		TerrainQuadTree(Device& device, ResourceManager& resourceManager,
			SyncContext& syncContext, const TerrainConfig& config);

		Handle<Texture> GetHeightAtlas() const { return _heightAtlas; }
		Handle<Texture> GetNormalAtlas() const { return _normalAtlas; }
		Handle<Texture> GetAlbedoAtlas() const { return _albedoAtlas; }
		Handle<Texture> GetSplatAtlas() const { return _splatAtlas; }

		uvec2 GetHeightAtlasExtent() const { return _heightExtent; }
		uvec2 GetColorAtlasExtent() const { return _colorExtent; }
		uvec2 GetSplatAtlasExtent() const { return _splatExtent; }

		// Slot lifetime (see TerrainSlotAllocator): a released slot may still
		// be sampled by in-flight frames, so it is retired, not freed.
		uint16_t AllocateSlot() { return _slots.Allocate(); }
		void ReleaseSlot(uint16_t slot) { _slots.Release(slot); }

		uvec2 HeightTexelOrigin(uint16_t slot) const;
		uvec2 ColorTexelOrigin(uint16_t slot) const;
		uvec2 SplatTexelOrigin(uint16_t slot) const;

		uint32_t GetFreeSlotCount() const { return _slots.GetFreeCount(); }

	private:
		uvec2 SlotCoord(uint16_t slot) const;

		const TerrainConfig& _config;
		uvec2 _heightExtent{};
		uvec2 _colorExtent{};
		uvec2 _splatExtent{};

		Handle<Texture> _heightAtlas;
		Handle<Texture> _normalAtlas;
		Handle<Texture> _albedoAtlas;
		Handle<Texture> _splatAtlas;

		TerrainSlotAllocator _slots;
	};
}
