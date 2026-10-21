#pragma once
#include "TerrainConfig.h"
#include "TerrainNodeStore.h"
#include "TerrainStreamer.h"
#include "TerrainQuadTree.h"
#include "TerrainMaterialSet.h"
#include "TerrainShadingCache.h"

namespace Core
{
	class Device;
	class ResourceManager;
	class PerspectiveCamera;
	class TransferContext;
	class SyncContext;
	class FrameResources;
	class BindlessTextureManager;

	// Facade over the terrain world structure: loads/bakes the node store at
	// startup and owns the quadtree GPU face, the streamer and the material
	// palette.
	class TerrainSystem
	{
	public:
		// Per-frame uniform blocks every terrain pass shares.
		static constexpr const char* UB_PARAMS = "Terrain.Params";
		static constexpr const char* UB_PALETTE = "Terrain.MaterialPalette";

		TerrainSystem(Device& device, ResourceManager& resourceManager,
			SyncContext& syncContext, TransferContext& transfer,
			BindlessTextureManager* bindless);

		void Update(PerspectiveCamera& camera);

		const TerrainConfig& GetConfig() const { return _config; }
		TerrainQuadTree& GetQuadTree() { return *_quadTree; }
		TerrainStreamer& GetStreamer() { return *_streamer; }
		TerrainMaterialSet& GetMaterials() { return *_materials; }
		TerrainShadingCache& GetShadingCache() { return *_shadingCache; }

		// Everything but the viewport, which the binding pass fills in.
		TerrainParams BuildRenderParams() const;

		const TerrainShadowSettings& GetShadowSettings() const { return _shadowSettings; }

		// Shared 17x17 patch grid, indexed by the GPU-driven instanced draw.
		Handle<Buffer> GetGridIndexBuffer() const { return _gridIndexBuffer; }
		uint32_t GetGridIndexCount() const { return _gridIndexCount; }

		void QueuePendingInit(Device& device, FrameResources& frameResources);

		// Stats feed for the visible patch count: the patch cull pass copies
		// the GPU-culled instanceCount into the frame's slot; OnGUI reads the
		// slot whose submission Begin's wait already retired (2-frame delay).
		Handle<Buffer> GetPatchCountReadback(uint32_t slot) const
		{
			return _patchCountReadback[slot];
		}

		// Debug: the patch cull keeps culled patches tagged, the terrain pass
		// tints them and reports the one under the mouse into the frame's slot.
		// The pick buffers exist only once the mode was enabled.
		bool IsCullingBypassed() const { return _bypassCulling; }
		Handle<Buffer> GetPickReadback(uint32_t slot) const { return _pickReadback[slot]; }

		void OnGUI();
		bool IsWireframe() const { return _wireframe; }
		int GetDebugMode() const { return _debugMode; }

	private:
		void CreateGridIndexBuffer(ResourceManager& resourceManager);
		void CreatePickReadback();
		void OnGUIPick(uint32_t slot);

		ResourceManager& _resourceManager;
		TerrainConfig _config;
		unique_ptr<TerrainMaterialSet> _materials;
		TerrainNodeStore _store;
		unique_ptr<TerrainQuadTree> _quadTree;
		unique_ptr<TerrainStreamer> _streamer;
		unique_ptr<TerrainShadingCache> _shadingCache;

		Handle<Buffer> _gridIndexBuffer;
		uint32_t _gridIndexCount = 0;

		vector<uint16_t> _pendingGridIndices;

		array<Handle<Buffer>, MAX_FRAMES_IN_FLIGHT> _patchCountReadback;
		array<Handle<Buffer>, MAX_FRAMES_IN_FLIGHT> _pickReadback;

		bool _wireframe = false;
		bool _freezeStreaming = false;
		bool _bypassCulling = false;
		int _debugMode = 0;
		TerrainShadowSettings _shadowSettings;
		TerrainDetailSettings _detailSettings;
	};
}
