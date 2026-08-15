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

	// Facade over the terrain world structure: bakes the node store at
	// startup, owns the atlases and streamer, and builds the per-frame
	// covering set of resident nodes to render.
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

		const vector<TerrainNodeInstance>& GetRenderList() const { return _renderList; }
		const array<uint32_t, 8>& GetRenderListPerLod() const { return _renderListPerLod; }

		Handle<Buffer> GetGridIndexBuffer() const { return _gridIndexBuffer; }
		uint32_t GetGridIndexCount() const { return _gridIndexCount; }

		void OnGUI();
		bool IsWireframe() const { return _wireframe; }
		int GetDebugMode() const { return _debugMode; }

		// Validation stat fed back by TerrainNodeListPass (2 frames stale):
		// must converge to the CPU covering-set count when the camera rests.
		void SetGpuNodeCountStat(uint32_t count);

		// Bring-up validation for TerrainLodMapPass: compares the GPU map
		// (2 frames stale) against the CPU covering-set expectation.
		void ValidateGpuLodMap(const uint8_t* gpuMap);

	private:
		void BuildRenderList(vec2 cameraXZ, const mat4& viewProj);
		void VisitNode(const TerrainNodeId& id, vec2 cameraXZ);
		float DistanceToNodeXZ(vec2 point, const TerrainNodeId& id) const;
		bool IsNodeVisible(const TerrainNodeId& id) const;
		
		// Covering-set size WITHOUT frustum culling: the CPU reference the GPU
		// node list is validated against (same traversal as the compute).
		uint32_t CountCoveringSet(const TerrainNodeId& id, vec2 cameraXZ);
		void CreateGridIndexBuffer(Device& device, GeometryCopyQueue& geometryCopyQueue);

		TerrainConfig _config;
		TerrainNodeStore _store;
		unique_ptr<TerrainQuadTree> _quadTree;
		unique_ptr<TerrainStreamer> _streamer;

		vector<TerrainNodeInstance> _renderList;
		array<uint32_t, 8> _renderListPerLod{};
		array<vec4, 6> _frustumPlanes{};
		uint32_t _culledNodes = 0;
		uint32_t _coveringNodeCount = 0;
		uint32_t _gpuNodeCount = 0;
		vector<uint8_t> _expectedLodMap; // per LOD0 sector, from CountCoveringSet
		uint32_t _gpuLodMapMismatches = 0;

		Handle<Buffer> _gridIndexBuffer;
		uint32_t _gridIndexCount = 0;

		bool _wireframe = false;
		bool _freezeStreaming = false;
		int _debugMode = 0;
	};
}
