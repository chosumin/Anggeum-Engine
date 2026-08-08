#include "stdafx.h"
#include "TerrainNoise.h"


namespace
{
	uint32_t Hash(uint32_t x)
	{
		x ^= x >> 16;
		x *= 0x7feb352d;
		x ^= x >> 15;
		x *= 0x846ca68b;
		x ^= x >> 16;
		return x;
	}
}

namespace Core
{
	float TerrainNoise::Gradient(ivec2 cell, vec2 offset, uint32_t seed)
	{
		uint32_t h = Hash(uint32_t(cell.x) * 0x8da6b343u
			^ uint32_t(cell.y) * 0xd8163841u ^ seed);
		float angle = float(h) * (6.2831853f / 4294967296.0f);
		return cos(angle) * offset.x + sin(angle) * offset.y;
	}

	float TerrainNoise::Noise(vec2 p, uint32_t seed)
	{
		ivec2 cell = ivec2(floor(p));
		vec2 f = p - vec2(cell);
		vec2 u = f * f * (3.0f - 2.0f * f);

		float g00 = Gradient(cell, f, seed);
		float g10 = Gradient(cell + ivec2(1, 0), f - vec2(1, 0), seed);
		float g01 = Gradient(cell + ivec2(0, 1), f - vec2(0, 1), seed);
		float g11 = Gradient(cell + ivec2(1, 1), f - vec2(1, 1), seed);

		return mix(mix(g00, g10, u.x), mix(g01, g11, u.x), u.y);
	}

	float TerrainNoise::Fbm(vec2 worldXZ, const TerrainNoiseParams& params)
	{
		vec2 p = worldXZ * params.baseFrequency;
		float amplitude = 1.0f;
		float total = 0.0f;
		float totalAmplitude = 0.0f;

		for (int octave = 0; octave < params.octaves; ++octave)
		{
			total += amplitude * Noise(p, params.seed + uint32_t(octave));
			totalAmplitude += amplitude;
			p *= params.lacunarity;
			amplitude *= params.gain;
		}

		return glm::clamp(total / totalAmplitude * params.amplitude, -1.0f, 1.0f);
	}
}
