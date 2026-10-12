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

	uint32_t PackRGBA8(vec4 color)
	{
		uvec4 c = uvec4(clamp(color, 0.0f, 1.0f) * 255.0f + 0.5f);
		return c.r | (c.g << 8) | (c.b << 16) | (c.a << 24);
	}

	float Dither(uint32_t x, uint32_t y)
	{
		uint32_t h = (x * 0x8da6b343u) ^ (y * 0xd8163841u);
		h ^= h >> 13; h *= 0x9E3779B1u; h ^= h >> 16;
		return (float(h & 0xffffu) / 65535.0f - 0.5f);
	}

	struct SurfaceSample
	{
		vec3 albedo;
		float roughness;
	};

	// Biome rules from height and slope; albedo and roughness share the
	// weights so a texel's material reads consistently.
	SurfaceSample ShadeSurface(float heightMeters, float slopeY, float dither,
		const TerrainConfig& config)
	{
		const vec3 grass{ 0.22f, 0.34f, 0.12f };
		const vec3 dirt{ 0.32f, 0.26f, 0.18f };
		const vec3 rock{ 0.42f, 0.40f, 0.38f };
		const vec3 snow{ 0.90f, 0.92f, 0.95f };

		float heightT = (heightMeters - config.heightMin)
			/ (config.heightMax - config.heightMin);

		float dirtT = smoothstep(0.55f, 0.7f, heightT);
		float rockT = smoothstep(0.95f, 0.85f, slopeY);
		float snowT = smoothstep(0.78f, 0.88f, heightT) * smoothstep(0.7f, 0.9f, slopeY);

		vec3 color = mix(grass, dirt, dirtT);
		color = mix(color, rock, rockT);
		color = mix(color, snow, snowT);

		float roughness = mix(0.95f, 0.9f, dirtT);
		roughness = mix(roughness, 0.7f, rockT);
		roughness = mix(roughness, 0.55f, snowT);

		return { color + dither * 0.02f, roughness };
	}

	// Horizon search radius in texels: the height grid's apron must cover it.
	constexpr int kAoRadiusTexels = 8;

	// Sky visibility from the horizon in 8 directions, HBAO-style against the
	// tangent plane: occlusion = sin(horizon) - sin(tangent) per direction.
	float HorizonAO(const vector<float>& heights, int gridSize, int gx, int gy,
		vec3 normal, float spacing)
	{
		static const ivec2 kDirections[8] = {
			{ 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 },
			{ 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };

		float h0 = heights[gy * gridSize + gx];
		float occlusion = 0.0f;
		for (const ivec2& d : kDirections)
		{
			float dirLength = length(vec2(d));
			// Rise per meter of the tangent plane along d (normal ∝ (-dH/dx, 1, -dH/dz)).
			float slope = -(normal.x * float(d.x) + normal.z * float(d.y))
				/ (normal.y * dirLength);
			float sinTangent = slope / sqrt(1.0f + slope * slope);

			float sinHorizon = sinTangent;
			for (int k = 1; k <= kAoRadiusTexels; ++k)
			{
				float dh = heights[(gy + d.y * k) * gridSize + gx + d.x * k] - h0;
				float dist = spacing * dirLength * float(k);
				sinHorizon = std::max(sinHorizon, dh / sqrt(dh * dh + dist * dist));
			}
			occlusion += sinHorizon - sinTangent;
		}
		return glm::clamp(1.0f - occlusion / 8.0f, 0.0f, 1.0f);
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

		// Sample heights once on a color-texel-centered grid with an apron wide
		// enough for the AO horizon walk (and the normals' central
		// differences). Color texel i center maps to local fraction
		// (i - border + 0.5) / quadCountPerNodeEdge; apron texels sample
		// outside the node, which the global noise makes valid - and the
		// lattice is the same for every node of a LOD, so AO stays seamless.
		const int apron = kAoRadiusTexels;
		const int gridSize = int(colorTexels) + 2 * apron;
		vector<float> heights(size_t(gridSize) * gridSize);
		for (int j = 0; j < gridSize; ++j)
			for (int i = 0; i < gridSize; ++i)
			{
				vec2 local = (vec2(float(i), float(j)) - float(apron) - float(border) + 0.5f)
					/ float(config.quadCountPerNodeEdge);
				heights[size_t(j) * gridSize + i] =
					heightSource.Sample(nodeOrigin + local * nodeSize);
			}

		payload.normal.resize(colorTexels * colorTexels);
		payload.albedo.resize(colorTexels * colorTexels);
		for (uint32_t j = 0; j < colorTexels; ++j)
			for (uint32_t i = 0; i < colorTexels; ++i)
			{
				int gx = int(i) + apron, gy = int(j) + apron;
				float dHdx = heights[gy * gridSize + gx + 1]
					- heights[gy * gridSize + gx - 1];
				float dHdz = heights[(gy + 1) * gridSize + gx]
					- heights[(gy - 1) * gridSize + gx];
				vec3 normal = normalize(vec3(-dHdx, 2.0f * spacing, -dHdz));
				float ao = HorizonAO(heights, gridSize, gx, gy, normal, spacing);

				SurfaceSample surface = ShadeSurface(
					heights[gy * gridSize + gx], normal.y,
					Dither(uint32_t(id.x) * colorTexels + i,
						uint32_t(id.y) * colorTexels + j),
					config);

				uint32_t texel = j * colorTexels + i;
				payload.normal[texel] = PackRGBA8(vec4(
					normal.x * 0.5f + 0.5f, normal.z * 0.5f + 0.5f, surface.roughness, ao));
				payload.albedo[texel] = PackRGBA8(vec4(surface.albedo, 1.0f));
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
