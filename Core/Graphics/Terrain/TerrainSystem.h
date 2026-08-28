#pragma once
#include "TerrainConfig.h"
#include "TerrainNodeStore.h"
#include "TerrainStreamer.h"
#include "TerrainQuadTree.h"

namespace Core
{
	class Device;
	class PerspectiveCamera;
	class GeometryCopyQueue;
	class TransferContext;
	class Light;

	// Facade over the terrain world structure: loads/bakes the node store at
	// startup and owns the quadtree GPU face and the streamer.
	class TerrainSystem
	{
	public:
		TerrainSystem(Device& device, TransferContext& transfer);

		void Update(PerspectiveCamera& camera);

		const TerrainConfig& GetConfig() const { return _config; }
		TerrainQuadTree& GetQuadTree() { return *_quadTree; }
		TerrainStreamer& GetStreamer() { return *_streamer; }

		TerrainParams BuildRenderParams(Light* mainLight) const;

		// Shared 17x17 patch grid, indexed by the GPU-driven instanced draw.
		Handle<Buffer> GetGridIndexBuffer() const { return _gridIndexBuffer; }
		uint32_t GetGridIndexCount() const { return _gridIndexCount; }

		// Stats feed for the visible patch count: the patch cull pass copies
		// the GPU-culled instanceCount into the frame's slot; OnGUI reads the
		// slot whose submission Begin's wait already retired (2-frame delay).
		Handle<Buffer> GetPatchCountReadback(uint32_t slot) const
		{
			return _patchCountReadback[slot];
		}

		void OnGUI();
		bool IsWireframe() const { return _wireframe; }
		int GetDebugMode() const { return _debugMode; }

	private:
		void CreateGridIndexBuffer(Device& device, GeometryCopyQueue& geometryCopyQueue);

		TerrainConfig _config;
		TerrainNodeStore _store;
		unique_ptr<TerrainQuadTree> _quadTree;
		unique_ptr<TerrainStreamer> _streamer;

		Handle<Buffer> _gridIndexBuffer;
		uint32_t _gridIndexCount = 0;

		array<Handle<Buffer>, MAX_FRAMES_IN_FLIGHT> _patchCountReadback;

		bool _wireframe = false;
		bool _freezeStreaming = false;
		int _debugMode = 0;
	};
}
