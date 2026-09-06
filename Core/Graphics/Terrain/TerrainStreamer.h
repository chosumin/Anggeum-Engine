#pragma once
#include "TerrainNode.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
	class Device;
	class Buffer;
	class Sampler;
	class TerrainNodeStore;
	class TerrainQuadTree;
	class TransferContext;
	class FrameResources;
	class ResourceManager;

	struct TerrainStreamingStats
	{
		uint32_t requested = 0;
		uint32_t resident = 0;
		uint32_t pending = 0;
		uint32_t starved = 0;   // requested but no atlas slot available
		uint32_t uploadedThisFrame = 0;
		uint32_t evictedThisFrame = 0;
	};

	// CPU streaming: ring-based requests around the camera, load-state diff, and budgeted uploads. 
	// The requested state and the actual state are allowed to diverge; 
	// the renderer only ever consumes Resident nodes.
	class TerrainStreamer
	{
	public:
		TerrainStreamer(const TerrainConfig& config, const TerrainNodeStore& store,
			TerrainQuadTree& quadTree, TransferContext& transfer,
			ResourceManager& resourceManager);
		~TerrainStreamer();

		// Main thread, once per frame, before frame-graph Setup.
		void Update(vec2 cameraXZ);

		void QueueTableInit(Device& device, FrameResources& frameResources);

		bool IsResident(const TerrainNodeId& id) const
		{
			return _runtime[ToLinearIndex(id, _config)].state
				== TerrainNodeState::Resident;
		}

		uint16_t GetAtlasSlot(const TerrainNodeId& id) const
		{
			return _runtime[ToLinearIndex(id, _config)].atlasSlot;
		}

		float LoadRadius(uint32_t lod) const
		{
			return _config.ringRadiusScale * _config.NodeSize(lod);
		}

		const TerrainStreamingStats& GetStats() const { return _stats; }

	private:
		// Distance from a point to a node's XZ bounds (0 inside).
		float DistanceToNode(vec2 point, const TerrainNodeId& id) const;
		void CountResident();
		bool IsRequested(vec2 cameraXZ, const TerrainNodeId& id, float radiusScale) const;
		// The bookkeeping half of a tile load: runtime state, table mirrors.
		void RegisterNode(const TerrainNodeId& id, uint16_t slot);

		const TerrainConfig& _config;
		const TerrainNodeStore& _store;
		TerrainQuadTree& _quadTree;
		TransferContext& _transfer;

		vector<TerrainNodeRuntime> _runtime;             // by global linear index
		vector<vector<uint16_t>> _indexMirror;           // CPU copy, per LOD
		vector<TerrainNodeDescGPU> _descMirror;          // by atlas slot

		bool _atlasLayoutPending = true;

		Handle<Sampler> _indexSampler;

		TerrainStreamingStats _stats;
	};
}
