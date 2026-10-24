#include "stdafx.h"
#include "ImageTerrainHeightSource.h"
#include "Utils/FileSystem.h"
#include "Utils/Log.h"

namespace
{
	using namespace Core;

	struct HeightMapDescriptor
	{
		filesystem::path dataPath;
		uvec2 size{};
		float heightMin = 0.0f;
		float heightMax = 0.0f;
	};

	bool ReadDescriptor(const string& descriptorPath, HeightMapDescriptor& out)
	{
		if (!FileSystem::Exists(descriptorPath))
		{
			LOG("Terrain height map: descriptor %s not found", descriptorPath.c_str());
			return false;
		}

		vector<char> text = FileSystem::ReadChar(descriptorPath);
		text.push_back('\0');
		Document doc;
		doc.Parse(text.data());

		auto number = [&](const char* name, float& value)
			{
				if (!doc.HasMember(name) || !doc[name].IsNumber())
					return false;
				value = doc[name].GetFloat();
				return true;
			};

		float width = 0.0f, height = 0.0f;
		if (doc.HasParseError() || !doc.IsObject()
			|| !doc.HasMember("data") || !doc["data"].IsString()
			|| !number("width", width) || !number("height", height)
			|| !number("heightMin", out.heightMin) || !number("heightMax", out.heightMax))
		{
			LOG("Terrain height map: %s is not a valid descriptor", descriptorPath.c_str());
			return false;
		}

		out.size = uvec2(uint32_t(width), uint32_t(height));
		out.dataPath = filesystem::path(descriptorPath).parent_path() / doc["data"].GetString();
		if (out.size.x < 2 || out.size.y < 2 || out.heightMax <= out.heightMin)
		{
			LOG("Terrain height map: %s has a degenerate size or range", descriptorPath.c_str());
			return false;
		}

		if (!filesystem::exists(out.dataPath))
		{
			LOG("Terrain height map: data file %s not found", out.dataPath.string().c_str());
			return false;
		}
		return true;
	}

	uint32_t HashFile(const filesystem::path& path, uint32_t hash)
	{
		string text = path.string();
		hash = TerrainHashBytes(text.data(), text.size(), hash);
		auto stamp = filesystem::last_write_time(path).time_since_epoch().count();
		return TerrainHashBytes(&stamp, sizeof(stamp), hash);
	}
}

namespace Core
{
	bool ImageTerrainHeightSource::ReadHeightRange(const string& descriptorPath,
		float& heightMin, float& heightMax)
	{
		HeightMapDescriptor desc;
		if (!ReadDescriptor(descriptorPath, desc))
			return false;
		heightMin = desc.heightMin;
		heightMax = desc.heightMax;
		return true;
	}

	unique_ptr<ImageTerrainHeightSource> ImageTerrainHeightSource::Load(
		const TerrainConfig& config, const TerrainFlatSpot& flat)
	{
		HeightMapDescriptor desc;
		if (!ReadDescriptor(config.heightMapPath, desc))
			return nullptr;

		size_t texelCount = size_t(desc.size.x) * desc.size.y;
		vector<uint8_t> bytes = FileSystem::Read(desc.dataPath.string());
		if (bytes.size() != texelCount * sizeof(uint16_t))
		{
			LOG("Terrain height map: %s holds %zu bytes, expected %zu (%ux%u R16)",
				desc.dataPath.string().c_str(), bytes.size(), texelCount * sizeof(uint16_t),
				desc.size.x, desc.size.y);
			return nullptr;
		}

		vector<uint16_t> texels(texelCount);
		memcpy(texels.data(), bytes.data(), bytes.size());

		uint32_t fileKey = HashFile(config.heightMapPath, 2166136261u);
		fileKey = HashFile(desc.dataPath, fileKey);

		LOG("Terrain height map: %s, %ux%u, range [%.1f, %.1f] m%s",
			config.heightMapPath.c_str(), desc.size.x, desc.size.y,
			desc.heightMin, desc.heightMax,
			config.heightMapDetailAmplitude > 0.0f ? " + FBM detail" : "");

		return unique_ptr<ImageTerrainHeightSource>(new ImageTerrainHeightSource(
			config, flat, std::move(texels), desc.size, fileKey));
	}

	ImageTerrainHeightSource::ImageTerrainHeightSource(const TerrainConfig& config,
		const TerrainFlatSpot& flat, vector<uint16_t>&& texels, uvec2 size, uint32_t fileKey)
		: _texels(std::move(texels))
		, _size(size)
		, _worldOrigin(config.WorldOrigin())
		, _worldExtent(vec2(float(config.rootTilesX), float(config.rootTilesZ)) * config.rootNodeSize)
		, _heightMin(config.heightMin)
		, _heightMax(config.heightMax)
		, _detailAmplitude(config.heightMapDetailAmplitude)
		, _flat(flat)
		, _fileKey(fileKey)
	{
		// Relief finer than a coarse DEM resolves: 64 m down to 8 m.
		_detailParams.baseFrequency = 1.0f / 64.0f;
		_detailParams.octaves = 4;
	}

	float ImageTerrainHeightSource::Sample(vec2 worldXZ) const
	{
		vec2 uv = glm::clamp((worldXZ - _worldOrigin) / _worldExtent, 0.0f, 1.0f);
		vec2 p = uv * vec2(_size - 1u);
		uvec2 i0 = uvec2(glm::floor(p));
		uvec2 i1 = glm::min(i0 + 1u, _size - 1u);
		vec2 f = p - vec2(i0);

		auto at = [&](uint32_t x, uint32_t y)
			{
				return float(_texels[size_t(y) * _size.x + x]);
			};
		float h = glm::mix(glm::mix(at(i0.x, i0.y), at(i1.x, i0.y), f.x),
			glm::mix(at(i0.x, i1.y), at(i1.x, i1.y), f.x), f.y) / 65535.0f;
		float height = glm::mix(_heightMin, _heightMax, h);

		if (_detailAmplitude > 0.0f)
		{
			height += TerrainNoise::Fbm(worldXZ, _detailParams) * _detailAmplitude;
			height = glm::clamp(height, _heightMin, _heightMax);
		}
		return _flat.Apply(worldXZ, height);
	}

	uint32_t ImageTerrainHeightSource::CacheKey() const
	{
		uint32_t hash = _fileKey;
		uint32_t size[2] = { _size.x, _size.y };
		hash = TerrainHashBytes(size, sizeof(size), hash);
		// Explicit floats: aligned gentypes pad vec2.
		float world[4] = { _worldOrigin.x, _worldOrigin.y, _worldExtent.x, _worldExtent.y };
		hash = TerrainHashBytes(world, sizeof(world), hash);
		float range[3] = { _heightMin, _heightMax, _detailAmplitude };
		hash = TerrainHashBytes(range, sizeof(range), hash);
		static_assert(sizeof(TerrainNoiseParams) == 6 * 4,
			"update CacheKey when TerrainNoiseParams changes");
		hash = TerrainHashBytes(&_detailParams, sizeof(_detailParams), hash);
		float flat[5] = { _flat.centerXZ.x, _flat.centerXZ.y, _flat.radius, _flat.falloff, _flat.height };
		return TerrainHashBytes(flat, sizeof(flat), hash);
	}
}
