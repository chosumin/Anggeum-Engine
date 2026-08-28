#pragma once

namespace Core
{
	struct TerrainNoiseParams
	{
		uint32_t seed = 1337;
		float baseFrequency = 1.0f / 900.0f;
		int octaves = 6;
		float lacunarity = 2.0f;
		float gain = 0.5f;
		// Raw normalized FBM rarely leaves [-0.4, 0.4]; without this gain the
		// relief reads as a flat plain.
		float amplitude = 1.8f;
	};

	// Deterministic 2D gradient noise over world XZ. Being a pure function of
	// world position is what guarantees cross-node and cross-LOD continuity.
	class TerrainNoise
	{
	public:
		// Fractal noise shaped to [-1, 1].
		static float Fbm(vec2 worldXZ, const TerrainNoiseParams& params);

	private:
		static float Gradient(ivec2 cell, vec2 offset, uint32_t seed);
		static float Noise(vec2 p, uint32_t seed);
	};
}
