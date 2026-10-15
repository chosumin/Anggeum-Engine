#include "stdafx.h"
#include "TerrainDetailBaker.h"
#include "TerrainNoise.h"

namespace
{
	using namespace Core;

	uint8_t Pack8(float v)
	{
		return uint8_t(glm::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
	}

	void PackRGBA8(uint8_t* dst, vec4 c)
	{
		dst[0] = Pack8(c.r); dst[1] = Pack8(c.g); dst[2] = Pack8(c.b); dst[3] = Pack8(c.a);
	}

	vec4 UnpackRGBA8(const uint8_t* src)
	{
		return vec4(src[0], src[1], src[2], src[3]) / 255.0f;
	}

	// 2x2 box filter. Normal maps average the decoded vectors and renormalize
	// so coarse mips do not flatten toward (0.5, 0.5, 0.5).
	vector<uint8_t> Downsample(const vector<uint8_t>& src, uint32_t srcSize,
		uint32_t bytesPerTexel, bool isNormal)
	{
		uint32_t dstSize = std::max(srcSize / 2, 1u);
		vector<uint8_t> dst(size_t(dstSize) * dstSize * bytesPerTexel);
		for (uint32_t y = 0; y < dstSize; ++y)
			for (uint32_t x = 0; x < dstSize; ++x)
			{
				const uint8_t* s00 = &src[(size_t(y * 2) * srcSize + x * 2) * bytesPerTexel];
				const uint8_t* s10 = s00 + bytesPerTexel;
				const uint8_t* s01 = s00 + size_t(srcSize) * bytesPerTexel;
				const uint8_t* s11 = s01 + bytesPerTexel;
				uint8_t* d = &dst[(size_t(y) * dstSize + x) * bytesPerTexel];

				if (bytesPerTexel == 1)
				{
					d[0] = uint8_t((uint32_t(s00[0]) + s10[0] + s01[0] + s11[0] + 2) / 4);
					continue;
				}

				vec4 sum = UnpackRGBA8(s00) + UnpackRGBA8(s10) + UnpackRGBA8(s01) + UnpackRGBA8(s11);
				vec4 avg = sum * 0.25f;
				if (isNormal)
				{
					vec3 n = normalize(vec3(avg) * 2.0f - 1.0f);
					avg = vec4(n * 0.5f + 0.5f, avg.a);
				}
				PackRGBA8(d, avg);
			}
		return dst;
	}

	TerrainDetailMipChain BuildChain(vector<uint8_t>&& mip0, uint32_t size,
		uint32_t bytesPerTexel, bool isNormal)
	{
		TerrainDetailMipChain chain;
		chain.bytesPerTexel = bytesPerTexel;
		chain.mips.reserve(TerrainDetailBaker::MipCount(size));
		chain.mips.push_back(std::move(mip0));
		for (uint32_t s = size; s > 1; s /= 2)
			chain.mips.push_back(Downsample(chain.mips.back(), s, bytesPerTexel, isNormal));
		return chain;
	}
}

namespace Core
{
	uint32_t TerrainDetailBaker::MipCount(uint32_t size)
	{
		uint32_t count = 1;
		while (size > 1) { size /= 2; ++count; }
		return count;
	}

	TerrainDetailMaps TerrainDetailBaker::Bake(const TerrainProceduralMaterial& material, uint32_t size)
	{
		assert((size & (size - 1)) == 0 && "detail maps are power-of-two squares");

		// Relief first: everything else derives from it.
		const int period = std::max(1, int(material.frequency + 0.5f));
		vector<float> relief(size_t(size) * size); // 0..1
		for (uint32_t y = 0; y < size; ++y)
			for (uint32_t x = 0; x < size; ++x)
			{
				vec2 uv = (vec2(float(x), float(y)) + 0.5f) / float(size);
				float h = TerrainNoise::TileableFbm(uv, period, 5, 0.5f, material.seed);
				relief[size_t(y) * size + x] = h * 0.5f + 0.5f;
			}

		auto at = [&](int x, int y)
			{
				// Wrap: the maps tile, so the gradient does too.
				int mask = int(size) - 1;
				return relief[size_t(y & mask) * size + (x & mask)];
			};

		vector<uint8_t> albedo(size_t(size) * size * 4);
		vector<uint8_t> normal(size_t(size) * size * 4);
		vector<uint8_t> height(size_t(size) * size);
		const float reliefTexels = material.reliefScale * float(size);
		const float smoothness = 1.0f - material.bake.roughness;
		for (uint32_t y = 0; y < size; ++y)
			for (uint32_t x = 0; x < size; ++x)
			{
				size_t texel = size_t(y) * size + x;
				float h = relief[texel];
				vec2 uv = (vec2(float(x), float(y)) + 0.5f) / float(size);

				// Centered on farAlbedo: the relief leans toward the variation
				// tint on peaks and away in valleys; a fine speckle breaks the
				// smoothness of the low-frequency noise.
				float speckle = TerrainNoise::TileableFbm(uv, period * 8, 2, 0.5f,
					material.seed + 101u);
				vec3 color = material.bake.albedo
					+ (material.albedoVariation - material.bake.albedo) * (h - 0.5f)
					+ material.bake.albedo * speckle * 0.08f;
				PackRGBA8(&albedo[texel * 4], vec4(color, 1.0f));

				float dHdx = (at(int(x) + 1, int(y)) - at(int(x) - 1, int(y))) * 0.5f * reliefTexels;
				float dHdy = (at(int(x), int(y) + 1) - at(int(x), int(y) - 1)) * 0.5f * reliefTexels;
				vec3 n = normalize(vec3(-dHdx, -dHdy, 1.0f));
				PackRGBA8(&normal[texel * 4], vec4(n * 0.5f + 0.5f,
					glm::clamp(smoothness + (h - 0.5f) * 0.1f, 0.0f, 1.0f)));

				height[texel] = Pack8(h);
			}

		TerrainDetailMaps maps;
		maps.albedo = BuildChain(std::move(albedo), size, 4, false);
		maps.normal = BuildChain(std::move(normal), size, 4, true);
		maps.height = BuildChain(std::move(height), size, 1, false);
		return maps;
	}
}
