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
		const TerrainStreamer::FrameUploads& GetFrameUploads() const { return _uploads; }

		const vector<TerrainNodeInstance>& GetRenderList() const { return _renderList; }
		const array<uint32_t, 8>& GetRenderListPerLod() const { return _renderListPerLod; }

		Handle<Buffer> GetGridIndexBuffer() const { return _gridIndexBuffer; }
		uint32_t GetGridIndexCount() const { return _gridIndexCount; }

		void OnGUI();
		bool IsWireframe() const { return _wireframe; }
		int GetDebugMode() const { return _debugMode; }

	private:
		void BuildRenderList(vec2 cameraXZ, const mat4& viewProj);
		void VisitNode(const TerrainNodeId& id, vec2 cameraXZ);
		float DistanceToNodeXZ(vec2 point, const TerrainNodeId& id) const;
		bool IsNodeVisible(const TerrainNodeId& id) const;
		void CreateGridIndexBuffer(Device& device, GeometryCopyQueue& geometryCopyQueue);

		TerrainConfig _config;
		TerrainNodeStore _store;
		unique_ptr<TerrainQuadTree> _quadTree;
		unique_ptr<TerrainStreamer> _streamer;

		TerrainStreamer::FrameUploads _uploads;
		vector<TerrainNodeInstance> _renderList;
		array<uint32_t, 8> _renderListPerLod{};
		array<vec4, 6> _frustumPlanes{};
		uint32_t _culledNodes = 0;

		Handle<Buffer> _gridIndexBuffer;
		uint32_t _gridIndexCount = 0;

		bool _wireframe = false;
		bool _freezeStreaming = false;
		int _debugMode = 0;
	};
}
