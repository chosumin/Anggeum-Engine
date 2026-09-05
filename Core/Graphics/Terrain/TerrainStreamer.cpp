#include "stdafx.h"
#include "TerrainStreamer.h"
#include "TerrainNodeStore.h"
#include "TerrainQuadTree.h"
#include "TerrainUpload.h"
#include "Graphics/FrameCounter.h"
#include "Graphics/TransferContext.h"

namespace Core
{
	TerrainStreamer::TerrainStreamer(const TerrainConfig& config,
		const TerrainNodeStore& store, TerrainQuadTree& quadTree,
		TransferContext& transfer)
		: _config(config), _store(store), _quadTree(quadTree), _transfer(transfer)
	{
		_runtime.resize(config.TotalNodeCount());

		_indexMirror.resize(config.lodCount);
		for (uint32_t lod = 0; lod < config.lodCount; ++lod)
			_indexMirror[lod].assign(config.NodeCount(lod), TERRAIN_NODE_EMPTY);

		_descMirror.resize(config.atlasCapacity);
	}

	TerrainStreamer::~TerrainStreamer() = default;

	float TerrainStreamer::DistanceToNode(vec2 point, const TerrainNodeId& id) const
	{
		float size = _config.NodeSize(id.lod);
		vec2 nodeMin = _config.WorldOrigin() + vec2(float(id.x), float(id.y)) * size;
		vec2 closest = clamp(point, nodeMin, nodeMin + size);
		return length(point - closest);
	}

	bool TerrainStreamer::IsRequested(vec2 cameraXZ, const TerrainNodeId& id,
		float radiusScale) const
	{
		// The coarsest LOD is always resident so parent fallback terminates.
		if (id.lod == _config.lodCount - 1)
			return true;

		return DistanceToNode(cameraXZ, id)
			<= LoadRadius(id.lod) * radiusScale;
	}

	void TerrainStreamer::RegisterNode(const TerrainNodeId& id, uint16_t slot)
	{
		const TerrainNodePayload& payload = _store.Get(id);

		_runtime[ToLinearIndex(id, _config)].state = TerrainNodeState::Resident;
		_indexMirror[id.lod][uint32_t(id.y) * _config.NodesPerSide(id.lod) + id.x] = slot;
		_descMirror[slot].minMaxHeight =
			uint32_t(payload.minHeight) | (uint32_t(payload.maxHeight) << 16);
		_descMirror[slot].slotLod = (slot % _config.atlasSlotsPerRow)
			| ((slot / _config.atlasSlotsPerRow) << 8) | (uint32_t(id.lod) << 16);
		_tablesDirty = true;
	}

	void TerrainStreamer::Update(vec2 cameraXZ)
	{
		_stats = {};

		// Pass 1: diff requested state against runtime state.
		vector<TerrainNodeId> toLoad;
		for (uint32_t lod = 0; lod < _config.lodCount; ++lod)
		{
			uint32_t side = _config.NodesPerSide(lod);
			for (uint16_t y = 0; y < side; ++y)
				for (uint16_t x = 0; x < side; ++x)
				{
					TerrainNodeId id{ uint8_t(lod), x, y };
					TerrainNodeRuntime& runtime = _runtime[ToLinearIndex(id, _config)];

					if (IsRequested(cameraXZ, id, 1.0f))
					{
						++_stats.requested;
						if (runtime.state == TerrainNodeState::Unloaded)
							toLoad.push_back(id);
					}
					else if (runtime.state != TerrainNodeState::Unloaded
						&& !IsRequested(cameraXZ, id, _config.evictHysteresis))
					{
						_quadTree.ReleaseSlot(runtime.atlasSlot);
						runtime = {};
						_indexMirror[lod][uint32_t(y) * side + x] = TERRAIN_NODE_EMPTY;
						_tablesDirty = true;
						++_stats.evictedThisFrame;
					}
				}
		}

		// Pass 2: budgeted uploads, coarse LODs first, then near-to-far. The
		// coarse bias keeps the fallback chain intact while refining.
		sort(toLoad.begin(), toLoad.end(),
			[&](const TerrainNodeId& a, const TerrainNodeId& b)
			{
				if (a.lod != b.lod)
					return a.lod > b.lod;
				return DistanceToNode(cameraXZ, a) < DistanceToNode(cameraXZ, b);
			});

		// The tile cap converts to bytes and asks the SHARED upload budget:
		// a scene load spike that already staged its bytes this frame shrinks
		// what terrain may stream, and vice versa once loads are budgeted too.
		VkDeviceSize tileBytes =
			VkDeviceSize(_config.HeightTexels()) * _config.HeightTexels() * 2
			+ VkDeviceSize(_config.ColorTexels()) * _config.ColorTexels() * 4 * 2
			+ 64; // per-tile alignment slack
		VkDeviceSize tableBytes = _config.atlasCapacity * sizeof(TerrainNodeDescGPU)
			+ _config.TotalNodeCount() * sizeof(uint16_t)
			+ 16 * (_config.lodCount + 1);

		uint32_t budgetTiles = std::min<uint32_t>(_config.uploadBudgetPerFrame,
			uint32_t(toLoad.size()));
		if (budgetTiles > 0)
		{
			VkDeviceSize granted = _transfer.GrantUploadBudget(budgetTiles * tileBytes);
			budgetTiles = uint32_t(granted / tileBytes);
		}

		if (budgetTiles == 0 && !_tablesDirty)
		{
			// Nothing to upload this frame.
			_stats.pending += uint32_t(toLoad.size());
			CountResident();
			return;
		}

		// The span is acquired BEFORE any bookkeeping commits: a full ring
		// (load spike) skips the whole frame, so the tables never point at
		// tiles that were not uploaded. Everything retries next frame.
		StagingRing::Span span = _transfer.AcquireStagingSpan(
			budgetTiles * tileBytes + tableBytes);
		if (!span.IsValid())
		{
			_stats.pending += uint32_t(toLoad.size());
			CountResident();
			return;
		}

		// Pass 2b: commit the admitted tiles (bookkeeping only - the payload
		// memcpys and copy recording happen in the upload job on a worker).
		vector<TerrainTileUpload> tiles;
		tiles.reserve(budgetTiles);

		for (const TerrainNodeId& id : toLoad)
		{
			if (_stats.uploadedThisFrame >= budgetTiles)
			{
				++_stats.pending;
				continue;
			}

			TerrainNodeRuntime& runtime = _runtime[ToLinearIndex(id, _config)];
			uint16_t slot = _quadTree.AllocateSlot();
			if (slot == TERRAIN_NODE_EMPTY)
			{
				++_stats.starved;
				continue;
			}

			runtime.atlasSlot = slot;
			RegisterNode(id, slot);
			tiles.push_back({ id, slot });
			++_stats.uploadedThisFrame;
		}

		// Pass 3: one job carries the tiles and (when dirty) the republished
		// lookup tables, so GPU-visible residency changes atomically. The
		// table snapshots are copies - this frame's mirrors, immutable to the
		// job while the streamer moves on.
		TransferContext::PendingUpload upload;
		upload.job = make_unique<TerrainUploadJob>(_quadTree, _store, _config,
			std::move(tiles), _tablesDirty,
			vector<TerrainNodeDescGPU>(_descMirror),
			vector<vector<uint16_t>>(_indexMirror),
			_firstUpload, span);
		upload.lane = QueueType::Graphics;

		_transfer.SubmitJob(std::move(upload),
			"Terrain.Upload_" + std::to_string(FrameCounter::GetFrameNumber()));

		_tablesDirty = false;
		_firstUpload = false;

		CountResident();
	}

	void TerrainStreamer::CountResident()
	{
		for (const auto& runtime : _runtime)
			if (runtime.state == TerrainNodeState::Resident)
				++_stats.resident;
	}
}
