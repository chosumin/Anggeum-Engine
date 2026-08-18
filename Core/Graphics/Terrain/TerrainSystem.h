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
	class Light;

	// Facade over the terrain world structure: loads/bakes the node store at
	// startup and owns the quadtree GPU face and the streamer.
	class TerrainSystem
	{
	public:
		// The queue is RenderScene's: static geometry uploads go through the
		// shared path (drained by the next Sync), never ImmediateSubmit.
		TerrainSystem(Device& device, GeometryCopyQueue& geometryCopyQueue);

		void Update(PerspectiveCamera& camera);

		const TerrainConfig& GetConfig() const { return _config; }
		TerrainQuadTree& GetQuadTree() { return *_quadTree; }
		TerrainStreamer& GetStreamer() { return *_streamer; }
		const TerrainStreamer::FrameUploads& GetFrameUploads() const
		{
			return _streamer->GetFrameUploads();
		}

		TerrainParams BuildRenderParams(Light* mainLight) const;

		// Shared 17x17 patch grid, indexed by the GPU-driven instanced draw.
		Handle<Buffer> GetGridIndexBuffer() const { return _gridIndexBuffer; }
		uint32_t GetGridIndexCount() const { return _gridIndexCount; }

		// Visible patch count fed back by TerrainPatchCullPass (2 frames
		// stale), for the stats display.
		void SetGpuPatchCountStat(uint32_t count) { _gpuPatchCount = count; }

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

		uint32_t _gpuPatchCount = 0;

		bool _wireframe = false;
		bool _freezeStreaming = false;
		int _debugMode = 0;
	};
}
