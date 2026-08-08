#include "stdafx.h"
#include "TerrainNodeStore.h"
#include "TerrainBaker.h"
#include "TerrainHeightSource.h"
#include "Utils/FileSystem.h"
#include "Utils/Log.h"

namespace
{
	using namespace Core;

	constexpr uint32_t kTerrainBakeMagic = 0x4B414254u; // 'TBAK'
	constexpr uint32_t kTerrainBakeVersion = 1;
	constexpr const char* kTerrainBakeCachePath = "Assets/Cache/terrain_bake.tbake";

	struct TerrainBakeFileHeader
	{
		uint32_t magic = 0;
		uint32_t version = 0;
		uint32_t bakeKey = 0;    // config + height source identity
		uint32_t nodeCount = 0;
		uint32_t heightTexels = 0;
		uint32_t colorTexels = 0;
	};

	// Everything that changes baked output. Streaming/format fields are
	// deliberately excluded: they do not affect payload contents.
	uint32_t BakeKey(const TerrainConfig& config, const TerrainHeightSource& heightSource)
	{
		uint32_t hash = heightSource.CacheKey();
		hash = TerrainHashBytes(&config.rootTilesX, sizeof(config.rootTilesX), hash);
		hash = TerrainHashBytes(&config.rootTilesZ, sizeof(config.rootTilesZ), hash);
		hash = TerrainHashBytes(&config.rootNodeSize, sizeof(config.rootNodeSize), hash);
		hash = TerrainHashBytes(&config.lodCount, sizeof(config.lodCount), hash);
		hash = TerrainHashBytes(&config.quadCountPerNodeEdge,
			sizeof(config.quadCountPerNodeEdge), hash);
		hash = TerrainHashBytes(&config.borderTexels, sizeof(config.borderTexels), hash);
		hash = TerrainHashBytes(&config.heightMin, sizeof(config.heightMin), hash);
		hash = TerrainHashBytes(&config.heightMax, sizeof(config.heightMax), hash);
		return hash;
	}

	// Fixed per-node stride: every payload size derives from the config, so
	// the file supports random access (offset = header + index * stride) —
	// the seam where per-node disk streaming plugs in later.
	size_t NodeByteSize(const TerrainConfig& config)
	{
		return 2 * sizeof(uint16_t)
			+ size_t(config.HeightTexels()) * config.HeightTexels() * sizeof(uint16_t)
			+ 2 * size_t(config.ColorTexels()) * config.ColorTexels() * sizeof(uint32_t);
	}

	bool TryLoadBakeCache(const TerrainConfig& config, uint32_t bakeKey,
		vector<TerrainNodePayload>& payloads)
	{
		if (!FileSystem::Exists(kTerrainBakeCachePath))
			return false;

		vector<uint8_t> fileData;
		try
		{
			fileData = FileSystem::Read(kTerrainBakeCachePath);
		}
		catch (const std::exception&)
		{
			return false;
		}

		TerrainBakeFileHeader header{};
		if (fileData.size() < sizeof(header))
			return false;
		memcpy(&header, fileData.data(), sizeof(header));

		if (header.magic != kTerrainBakeMagic || header.version != kTerrainBakeVersion
			|| header.bakeKey != bakeKey
			|| header.nodeCount != config.TotalNodeCount()
			|| header.heightTexels != config.HeightTexels()
			|| header.colorTexels != config.ColorTexels())
			return false;

		if (fileData.size() != sizeof(header) + size_t(header.nodeCount) * NodeByteSize(config))
			return false;

		size_t heightCount = size_t(config.HeightTexels()) * config.HeightTexels();
		size_t colorCount = size_t(config.ColorTexels()) * config.ColorTexels();
		const uint8_t* cursor = fileData.data() + sizeof(header);

		payloads.resize(header.nodeCount);
		for (auto& payload : payloads)
		{
			auto read = [&cursor](void* dst, size_t bytes)
				{
					memcpy(dst, cursor, bytes);
					cursor += bytes;
				};

			read(&payload.minHeight, sizeof(uint16_t));
			read(&payload.maxHeight, sizeof(uint16_t));
			payload.height.resize(heightCount);
			read(payload.height.data(), heightCount * sizeof(uint16_t));
			payload.normal.resize(colorCount);
			read(payload.normal.data(), colorCount * sizeof(uint32_t));
			payload.albedo.resize(colorCount);
			read(payload.albedo.data(), colorCount * sizeof(uint32_t));
		}
		return true;
	}

	void SaveBakeCache(const TerrainConfig& config, uint32_t bakeKey,
		const vector<TerrainNodePayload>& payloads)
	{
		TerrainBakeFileHeader header{};
		header.magic = kTerrainBakeMagic;
		header.version = kTerrainBakeVersion;
		header.bakeKey = bakeKey;
		header.nodeCount = uint32_t(payloads.size());
		header.heightTexels = config.HeightTexels();
		header.colorTexels = config.ColorTexels();

		vector<uint8_t> fileData(sizeof(header) + payloads.size() * NodeByteSize(config));
		uint8_t* cursor = fileData.data();
		auto write = [&cursor](const void* src, size_t bytes)
			{
				memcpy(cursor, src, bytes);
				cursor += bytes;
			};

		write(&header, sizeof(header));
		for (const auto& payload : payloads)
		{
			write(&payload.minHeight, sizeof(uint16_t));
			write(&payload.maxHeight, sizeof(uint16_t));
			write(payload.height.data(), payload.height.size() * sizeof(uint16_t));
			write(payload.normal.data(), payload.normal.size() * sizeof(uint32_t));
			write(payload.albedo.data(), payload.albedo.size() * sizeof(uint32_t));
		}

		try
		{
			FileSystem::Write(kTerrainBakeCachePath, fileData.data(), fileData.size());
		}
		catch (const std::exception&)
		{
			LOG("Terrain store: failed to write cache %s", kTerrainBakeCachePath);
		}
	}
}

namespace Core
{
	TerrainNodeStore TerrainNodeStore::Load(const TerrainConfig& config,
		const TerrainHeightSource& heightSource)
	{
		uint32_t bakeKey = BakeKey(config, heightSource);

		auto start = chrono::high_resolution_clock::now();
		vector<TerrainNodePayload> payloads;
		if (TryLoadBakeCache(config, bakeKey, payloads))
		{
			auto elapsed = chrono::duration_cast<chrono::milliseconds>(
				chrono::high_resolution_clock::now() - start).count();
			LOG("Terrain store: %u nodes loaded from cache in %lld ms",
				uint32_t(payloads.size()), elapsed);
			return TerrainNodeStore(config, move(payloads));
		}

		payloads = TerrainBaker::Bake(config, heightSource);
		SaveBakeCache(config, bakeKey, payloads);
		return TerrainNodeStore(config, move(payloads));
	}
}
