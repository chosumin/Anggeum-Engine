#pragma once

#define MAX_FORWARD_LIGHT_COUNT 1000
#define MAX_POINT_LIGHT_PER_TILE 128
#define TILE_SIZE 16

struct alignas(16) CameraBuffer
{
	mat4 View;
	mat4 Projection;
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

	uint BasemapIndex;
	uint NormalmapIndex;
	uint MetallicRoughnessmapIndex;
};

struct alignas(16) GI
{
	uint shadowmapIndex;
	uint irradianceMapIndex;
	uint prefilterMapIndex;
	uint brdfLUTIndex;
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

// For GPU Driven Rendering
struct alignas(16) GPUMaterialData
{
	glm::vec4 albedo{ 1.0f, 1.0f, 1.0f, 1.0f };
	float metallic = 0.0f;
	float roughness = 0.5f;
	float ao = 1.0f;
	int flags = 1;  // bit 0: enabled

	int albedoTextureSet = 0;
	int metallicTextureSet = 0;
	int roughnessTextureSet = 0;
	int occlusionTextureSet = 0;
	int debugMode = 0;

	uint32_t basemapIndex = 0;
	uint32_t normalmapIndex = 0;
	uint32_t metallicRoughnessmapIndex = 0;

	glm::vec3 padding;  // 16-byte alignment
};

static_assert(sizeof(GPUMaterialData) == 80, "GPUMaterialData must be 80 bytes");

struct alignas(16) GPUObjectData
{
	glm::vec4 boundingSphere;  // xyz: center, w: radius
	uint32_t transformIndex;
};

struct alignas(16) GPUCullData
{
	glm::vec4 frustumPlanes[6];
	uint32_t drawCount;
};