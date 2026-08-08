#include "stdafx.h"
#include "TerrainBaker.h"
#include "TerrainHeightSource.h"
#include "Utils/Log.h"
#include <execution>
#include <numeric>

namespace
{
	using namespace Core;

	TerrainNodeId DecodeLinearIndex(uint32_t index, const TerrainConfig& config)
	{
		for (uint32_t lod = 0; lod < config.lodCount; ++lod)
		{
			uint32_t count = config.NodeCount(lod);
			uint32_t offset = config.LevelOffset(lod);

			if (index < offset + count)
			{
				uint32_t local = index - offset;
				uint32_t side = config.NodesPerSide(lod);
				return { uint8_t(lod), uint16_t(local % side), uint16_t(local / side) };
			}
		}

		assert(false);
		return {};
	}

	uint16_t PackHeight(float meters, const TerrainConfig& config)
	{
		float t = (meters - config.heightMin) / (config.heightMax - config.heightMin);
		return uint16_t(glm::clamp(t, 0.0f, 1.0f) * 65535.0f + 0.5f);
	}

	uint32_t PackRGBA8(vec3 color)
	{
		uvec3 c = uvec3(clamp(color, 0.0f, 1.0f) * 255.0f + 0.5f);
		return c.r | (c.g << 8) | (c.b << 16) | 0xff000000u;
	}

	float Dither(uint32_t x, uint32_t y)
	{
		uint32_t h = (x * 0x8da6b343u) ^ (y * 0xd8163841u);
		h ^= h >> 13; h *= 0x9E3779B1u; h ^= h >> 16;
		return (float(h & 0xffffu) / 65535.0f - 0.5f);
	}

	vec3 ShadeAlbedo(float heightMeters, float slopeY, float dither,
		const TerrainConfig& config)
	{
		const vec3 grass{ 0.22f, 0.34f, 0.12f };
		const vec3 dirt{ 0.32f, 0.26f, 0.18f };
		const vec3 rock{ 0.42f, 0.40f, 0.38f };
		const vec3 snow{ 0.90f, 0.92f, 0.95f };

		float heightT = (heightMeters - config.heightMin)
			/ (config.heightMax - config.heightMin);

		vec3 color = mix(grass, dirt, smoothstep(0.55f, 0.7f, heightT));
		color = mix(color, rock, smoothstep(0.95f, 0.85f, slopeY));
		color = mix(color, snow,
			smoothstep(0.78f, 0.88f, heightT) * smoothstep(0.7f, 0.9f, slopeY));
		return color + dither * 0.02f;
	}

	void BakeNode(const TerrainNodeId& id, TerrainNodePayload& payload,
		const TerrainConfig& config, const TerrainHeightSource& heightSource)
	{
		const uint32_t heightTexels = config.HeightTexels(); // 129
		const uint32_t colorTexels = config.ColorTexels();   // 132
		const uint32_t border = config.borderTexels;
		const float nodeSize = config.NodeSize(id.lod);
		const float spacing = nodeSize / float(config.quadCountPerNodeEdge);
		const vec2 nodeOrigin = config.WorldOrigin()
			+ vec2(float(id.x), float(id.y)) * nodeSize;

		// Height: texel (i, j) sits exactly on grid vertex (i, j). 129 = 128
		// quads + shared edge, and Fbm is a pure function of world position,
		// so neighboring nodes bake identical edge values by construction.
		payload.height.resize(heightTexels * heightTexels);
		uint16_t minHeight = 0xffff, maxHeight = 0;
		for (uint32_t j = 0; j < heightTexels; ++j)
			for (uint32_t i = 0; i < heightTexels; ++i)
			{
				vec2 world = nodeOrigin + vec2(float(i), float(j)) * spacing;
				uint16_t packed = PackHeight(heightSource.Sample(world), config);
				payload.height[j * heightTexels + i] = packed;
				minHeight = std::min(minHeight, packed);
				maxHeight = std::max(maxHeight, packed);
			}
		payload.minHeight = minHeight;
		payload.maxHeight = maxHeight;

		// Sample heights once on a (colorTexels + 2)^2 grid at color-texel
		// centers so normals reuse them for central differences. Color texel i
		// center maps to local fraction (i - border + 0.5) / quadCountPerNodeEdge; apron
		// texels sample outside the node, which the global noise makes valid.
		const uint32_t gridSize = colorTexels + 2;
		vector<float> heights(gridSize * gridSize);
		for (uint32_t j = 0; j < gridSize; ++j)
			for (uint32_t i = 0; i < gridSize; ++i)
			{
				vec2 local = (vec2(float(i), float(j)) - float(border) - 0.5f)
					/ float(config.quadCountPerNodeEdge);
				heights[j * gridSize + i] =
					heightSource.Sample(nodeOrigin + local * nodeSize);
			}

		payload.normal.resize(colorTexels * colorTexels);
		payload.albedo.resize(colorTexels * colorTexels);
		for (uint32_t j = 0; j < colorTexels; ++j)
			for (uint32_t i = 0; i < colorTexels; ++i)
			{
				uint32_t gx = i + 1, gy = j + 1;
				float dHdx = heights[gy * gridSize + gx + 1]
					- heights[gy * gridSize + gx - 1];
				float dHdz = heights[(gy + 1) * gridSize + gx]
					- heights[(gy - 1) * gridSize + gx];
				vec3 normal = normalize(vec3(-dHdx, 2.0f * spacing, -dHdz));

				uint32_t texel = j * colorTexels + i;
				payload.normal[texel] = PackRGBA8(normal * 0.5f + 0.5f);
				payload.albedo[texel] = PackRGBA8(ShadeAlbedo(
					heights[gy * gridSize + gx], normal.y,
					Dither(uint32_t(id.x) * colorTexels + i,
						uint32_t(id.y) * colorTexels + j),
					config));
			}
	}
}

namespace Core
{
	vector<TerrainNodePayload> TerrainBaker::Bake(const TerrainConfig& config,
		const TerrainHeightSource& heightSource)
	{
		auto start = chrono::high_resolution_clock::now();

		uint32_t totalNodes = config.TotalNodeCount();
		vector<TerrainNodePayload> payloads(totalNodes);

		vector<uint32_t> indices(totalNodes);
		iota(indices.begin(), indices.end(), 0u);
		for_each(execution::par, indices.begin(), indices.end(),
			[&](uint32_t index)
			{
				BakeNode(DecodeLinearIndex(index, config), payloads[index],
					config, heightSource);
			});

		uint16_t globalMin = 0xffff, globalMax = 0;
		for (const auto& payload : payloads)
		{
			globalMin = std::min(globalMin, payload.minHeight);
			globalMax = std::max(globalMax, payload.maxHeight);
		}

		auto elapsed = chrono::duration_cast<chrono::milliseconds>(
			chrono::high_resolution_clock::now() - start).count();
		LOG("Terrain bake: %u nodes in %lld ms, height range [%.1f, %.1f] m",
			totalNodes, elapsed,
			config.heightMin + (config.heightMax - config.heightMin)
			* float(globalMin) / 65535.0f,
			config.heightMin + (config.heightMax - config.heightMin)
			* float(globalMax) / 65535.0f);

		return payloads;
	}
}
