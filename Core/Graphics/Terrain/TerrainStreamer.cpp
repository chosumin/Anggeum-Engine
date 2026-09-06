#include "stdafx.h"
#include "TerrainStreamer.h"
#include "TerrainNodeStore.h"
#include "TerrainQuadTree.h"
#include "TerrainUpload.h"
#include "Graphics/BufferUpload.h"
#include "Graphics/FrameCounter.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/FrameResources.h"
#include "Graphics/TransferContext.h"

namespace Core
{
	TerrainStreamer::TerrainStreamer(const TerrainConfig& config,
		const TerrainNodeStore& store, TerrainQuadTree& quadTree,
		TransferContext& transfer, ResourceManager& resourceManager)
		: _config(config), _store(store), _quadTree(quadTree), _transfer(transfer)
	{
		_indexSampler = resourceManager.LoadSampler(
			{ VK_FILTER_NEAREST, VK_FILTER_NEAREST,
			  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			  VK_SAMPLER_MIPMAP_MODE_NEAREST });

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

		uint32_t budgetTiles = std::min<uint32_t>(_config.uploadBudgetPerFrame,
			uint32_t(toLoad.size()));
		if (budgetTiles > 0)
		{
			VkDeviceSize granted = _transfer.GrantUploadBudget(budgetTiles * tileBytes);
			budgetTiles = uint32_t(granted / tileBytes);
		}

		StagingRing::Span span;
		if (budgetTiles > 0)
			span = _transfer.AcquireStagingSpan(budgetTiles * tileBytes);

		if (span.IsValid())
		{
			// Pass 2b: commit the admitted tiles (bookkeeping only - the
			// payload memcpys and copy recording happen in the upload job on a
			// worker).
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

			// Pass 3: submit the tile pixels to the transfer queue and BLOCK
			// on the recording (a memcpy): the tables published below declare
			// these slots resident this frame, so the job must not slide past
			// this frame's Flush.
			string jobName =
				"Terrain.Upload_" + std::to_string(FrameCounter::GetFrameNumber());

			TransferContext::PendingUpload upload;
			upload.job = make_unique<TerrainUploadJob>(_quadTree, _store, _config,
				std::move(tiles), span, _atlasLayoutPending);
			_transfer.SubmitJob(std::move(upload), jobName);
			_transfer.WaitForRecording(jobName);
			_atlasLayoutPending = false;
		}
		else
		{
			_stats.pending += uint32_t(toLoad.size());
		}

		CountResident();
	}

	void TerrainStreamer::QueueTableInit(Device& device, FrameResources& frameResources)
	{
		// This frame slot's own table copies: created on first use, then
		// overwritten wholesale every frame from the live mirrors (stable
		// between Updates). Publishing every frame keeps each slot's copy
		// current - including evictions the same frame their release was
		// stamped, ring pressure notwithstanding.
		BufferDesc descDesc{
			_config.atlasCapacity * sizeof(TerrainNodeDescGPU),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			MemoryType::DEVICE_LOCAL };
		Handle<Buffer> descBuffer = frameResources.GetOrCreateStorageBuffer(
			TerrainQuadTree::NODE_DESC, descDesc);
		frameResources.AddInitJob(make_unique<BufferUploadJob<TerrainNodeDescGPU>>(
			device, descBuffer.Get(),
			vector<TerrainNodeDescGPU>(_descMirror), 0));

		RenderTargetDesc indexDesc{};
		indexDesc.extent = { _config.NodesPerSide(0), _config.NodesPerSide(0) };
		indexDesc.format = VK_FORMAT_R16_UINT;
		indexDesc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		indexDesc.mipLevels = _config.lodCount;
		indexDesc.sampler = _indexSampler;
		Handle<Texture> indexTexture = frameResources.GetOrCreateRenderTarget(
			TerrainQuadTree::QUADTREE_INDEX, indexDesc);
		frameResources.AddInitJob(make_unique<TerrainIndexUploadJob>(
			device, indexTexture.Get(), _config,
			vector<vector<uint16_t>>(_indexMirror)));
	}

	void TerrainStreamer::CountResident()
	{
		for (const auto& runtime : _runtime)
			if (runtime.state == TerrainNodeState::Resident)
				++_stats.resident;
	}
}
