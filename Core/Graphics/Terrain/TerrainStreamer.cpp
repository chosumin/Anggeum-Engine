#include "stdafx.h"
#include "TerrainStreamer.h"
#include "TerrainNodeStore.h"
#include "TerrainQuadTree.h"
#include "Graphics/RenderContext.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/Vulkans/Buffer.h"

namespace
{
	VkDeviceSize Align(VkDeviceSize offset, VkDeviceSize alignment)
	{
		return (offset + alignment - 1) & ~(alignment - 1);
	}

	VkBufferImageCopy MakeRegion(VkDeviceSize bufferOffset, glm::uvec2 texelOrigin,
		uint32_t extent, uint32_t mipLevel = 0)
	{
		VkBufferImageCopy region{};
		region.bufferOffset = bufferOffset;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = mipLevel;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = { int32_t(texelOrigin.x), int32_t(texelOrigin.y), 0 };
		region.imageExtent = { extent, extent, 1 };
		return region;
	}
}

namespace Core
{
	TerrainStreamer::TerrainStreamer(Device& device, const TerrainConfig& config,
		const TerrainNodeStore& store, TerrainQuadTree& quadTree)
		: _config(config), _store(store), _quadTree(quadTree)
	{
		_runtime.resize(config.TotalNodeCount());

		_indexMirror.resize(config.lodCount);
		for (uint32_t lod = 0; lod < config.lodCount; ++lod)
			_indexMirror[lod].assign(config.NodeCount(lod), TERRAIN_NODE_EMPTY);

		_descMirror.resize(config.atlasCapacity);

		// Budgeted tile payloads + the full desc table + all index mips.
		// UNIFORM memory type: persistently mapped by the allocator, which
		// avoids the concurrent-map hazard of the shared STAGE blocks.
		VkDeviceSize tileBytes =
			VkDeviceSize(config.HeightTexels()) * config.HeightTexels() * 2
			+ VkDeviceSize(config.ColorTexels()) * config.ColorTexels() * 4 * 2;
		VkDeviceSize stagingSize = Align(
			config.uploadBudgetPerFrame * (tileBytes + 64)
			+ config.atlasCapacity * sizeof(TerrainNodeDescGPU)
			+ config.TotalNodeCount() * sizeof(uint16_t) + 1024, 65536);

		for (uint32_t slot = 0; slot < MAX_FRAMES_IN_FLIGHT; ++slot)
			_staging[slot] = device.GetResourceManager().LoadBuffer(
				{ stagingSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryType::UNIFORM },
				"Terrain.Staging" + to_string(slot));
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

	void TerrainStreamer::UploadNode(const TerrainNodeId& id, uint8_t* stagingBase,
		VkDeviceSize& offset)
	{
		const TerrainNodePayload& payload = _store.Get(id);
		TerrainNodeRuntime& runtime = _runtime[ToLinearIndex(id, _config)];
		uint16_t slot = runtime.atlasSlot;

		auto append = [&](const void* data, VkDeviceSize bytes,
			vector<VkBufferImageCopy>& regions, uvec2 texelOrigin, uint32_t extent)
			{
				offset = Align(offset, 16);
				memcpy(stagingBase + offset, data, size_t(bytes));
				regions.push_back(MakeRegion(offset, texelOrigin, extent));
				offset += bytes;
			};

		uint32_t heightTexels = _config.HeightTexels();
		uint32_t colorTexels = _config.ColorTexels();
		append(payload.height.data(), payload.height.size() * 2,
			_uploads.heightRegions, _quadTree.HeightTexelOrigin(slot), heightTexels);
		append(payload.normal.data(), payload.normal.size() * 4,
			_uploads.normalRegions, _quadTree.ColorTexelOrigin(slot), colorTexels);
		append(payload.albedo.data(), payload.albedo.size() * 4,
			_uploads.albedoRegions, _quadTree.ColorTexelOrigin(slot), colorTexels);

		runtime.state = TerrainNodeState::Resident;
		_indexMirror[id.lod][uint32_t(id.y) * _config.NodesPerSide(id.lod) + id.x] = slot;
		_descMirror[slot].minMaxHeight =
			uint32_t(payload.minHeight) | (uint32_t(payload.maxHeight) << 16);
		_descMirror[slot].slotLod = (slot % _config.atlasSlotsPerRow)
			| ((slot / _config.atlasSlotsPerRow) << 8) | (uint32_t(id.lod) << 16);
		_tablesDirty = true;
	}

	const TerrainStreamer::FrameUploads& TerrainStreamer::Update(vec2 cameraXZ)
	{
		_uploads = {};
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

		Buffer& staging = _staging[FrameCounter::GetFrameNumber() % MAX_FRAMES_IN_FLIGHT].Get();
		uint8_t* stagingBase = nullptr;
		staging.GetMappedPtr(reinterpret_cast<void**>(&stagingBase));
		VkDeviceSize offset = 0;

		for (const TerrainNodeId& id : toLoad)
		{
			if (_stats.uploadedThisFrame >= _config.uploadBudgetPerFrame)
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
			UploadNode(id, stagingBase, offset);
			++_stats.uploadedThisFrame;
		}

		// Pass 3: republish the (tiny) GPU lookup tables when anything moved.
		if (_tablesDirty)
		{
			offset = Align(offset, 16);
			_uploads.descOffset = offset;
			VkDeviceSize descBytes = _descMirror.size() * sizeof(TerrainNodeDescGPU);
			memcpy(stagingBase + offset, _descMirror.data(), size_t(descBytes));
			offset += descBytes;
			_uploads.descDirty = true;

			for (uint32_t lod = 0; lod < _config.lodCount; ++lod)
			{
				offset = Align(offset, 16);
				VkDeviceSize mipBytes = _indexMirror[lod].size() * sizeof(uint16_t);
				memcpy(stagingBase + offset, _indexMirror[lod].data(), size_t(mipBytes));
				_uploads.indexRegions.push_back(MakeRegion(offset, uvec2(0),
					_config.NodesPerSide(lod), lod));
				offset += mipBytes;
			}
			_tablesDirty = false;
		}

		assert(offset <= staging.GetSize());
		_uploads.staging = &staging;

		for (const auto& runtime : _runtime)
			if (runtime.state == TerrainNodeState::Resident)
				++_stats.resident;

		return _uploads;
	}
}
