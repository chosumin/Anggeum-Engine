#pragma once
#include "TerrainNode.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
	class Device;
	class Buffer;
	class TerrainNodeStore;
	class TerrainQuadTree;

	struct TerrainStreamingStats
	{
		uint32_t requested = 0;
		uint32_t resident = 0;
		uint32_t pending = 0;
		uint32_t starved = 0;   // requested but no atlas slot available
		uint32_t uploadedThisFrame = 0;
		uint32_t evictedThisFrame = 0;
	};

	// CPU streaming: ring-based requests around the camera, load-state diff,
	// and budgeted staging writes. The requested state and the actual state
	// are allowed to diverge; the renderer only ever consumes Resident nodes.
	class TerrainStreamer
	{
	public:
		// Copies for the streaming pass to record this frame. Regions are
		// grouped per destination so the pass maps them onto atlas textures.
		struct FrameUploads
		{
			Buffer* staging = nullptr;
			vector<VkBufferImageCopy> heightRegions;
			vector<VkBufferImageCopy> normalRegions;
			vector<VkBufferImageCopy> albedoRegions;
			vector<VkBufferImageCopy> indexRegions;
			bool descDirty = false;
			VkDeviceSize descOffset = 0;

			bool Empty() const
			{
				return heightRegions.empty() && indexRegions.empty() && !descDirty;
			}
		};

		TerrainStreamer(Device& device, const TerrainConfig& config,
			const TerrainNodeStore& store, TerrainQuadTree& quadTree);
		~TerrainStreamer();

		// Main thread, once per frame, before frame-graph Setup.
		const FrameUploads& Update(vec2 cameraXZ);

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
		bool IsRequested(vec2 cameraXZ, const TerrainNodeId& id, float radiusScale) const;
		void UploadNode(const TerrainNodeId& id, uint8_t* stagingBase,
			VkDeviceSize& offset);

		const TerrainConfig& _config;
		const TerrainNodeStore& _store;
		TerrainQuadTree& _quadTree;

		vector<TerrainNodeRuntime> _runtime;             // by global linear index
		vector<vector<uint16_t>> _indexMirror;           // CPU copy, per LOD
		vector<TerrainNodeDescGPU> _descMirror;          // by atlas slot
		bool _tablesDirty = true;                        // desc + index textures

		array<Handle<Buffer>, MAX_FRAMES_IN_FLIGHT> _staging;
		FrameUploads _uploads;
		TerrainStreamingStats _stats;
	};
}
