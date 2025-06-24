#pragma once

#define MAX_FORWARD_LIGHT_COUNT 1000
#define MAX_POINT_LIGHT_PER_TILE 128
#define TILE_SIZE 16

struct alignas(16) VPBufferObject
{
	mat4 View;
	mat4 Perspective;
	vec3 Position;
};

struct ShadowUniform
{
	alignas(16) mat4 Projection;
};

struct alignas(16) PBRBuffer
{
	vec4 Albedo{ 1.0f, 1.0f, 1.0f, 1.0f };
	float Metallic;
	float Roughness;
	float AO;
	int AlbedoTextureSet;
	int MetallicTextureSet;
	int RoughnessTextureSet;
	int OcclusionTextureSet;
	int DebugMode;
};

struct alignas(16) LightInfo
{
	vec4 Position;  // position.w represents type of light
	vec4 Color;     // color.w represents light intensity
	vec4 Direction; // direction.w represents range
	vec2 Info;      // (only used for spot lights) info.x represents light inner cone angle, info.y represents light outer cone angle
};

struct LightBuffer
{
	LightInfo Light[MAX_FORWARD_LIGHT_COUNT];
	uint32_t Count;
};

struct IrradianceDelta
{
	float Phi;
	float Theta;
};

struct PrefilterEnv
{
	float Roughness;
	uint32_t NumSamples = 32u;
};

struct VisibleLightsForTile
{
	uint32_t count;
	std::array<uint32_t, MAX_POINT_LIGHT_PER_TILE> lightindices;
};

struct TileInfo
{
	ivec2 viewportSize;
	ivec2 tileNums;
};