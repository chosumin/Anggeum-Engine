#pragma once

#define MAX_FORWARD_LIGHT_COUNT 1000
#define MAX_POINT_LIGHT_PER_TILE 128
#define TILE_SIZE 16
#define SHADOW_MAP_CASCADE_COUNT 4
#define SHADOW_MAP_DIM 2048

// SDF Shadow Constants
#define SDF_VOLUME_DIM 128
#define SDF_MAX_MARCH_STEPS 64
#define SDF_SHADOW_SOFTNESS 8.0f

struct alignas(16) CameraBuffer
{
	mat4 View;
	mat4 Projection;
	vec3 Position;
};

struct alignas(16) CascadeUniform
{
	mat4 ViewProjection;
	float SplitDepth;
};

// std140 float array: each element occupies 16 bytes
struct Std140Float
{
	float value;
	float _pad[3];
};

struct alignas(16) ShadowUniform
{
	mat4 ViewProjection[SHADOW_MAP_CASCADE_COUNT];
	Std140Float SplitDepth[SHADOW_MAP_CASCADE_COUNT];
	uint32_t CascadeCount = SHADOW_MAP_CASCADE_COUNT;

	// PCSS parameters (adjustable via ImGui)
	float LightSize = 0.04f;
	float MinFilterRadius = 0.5f;
	float MaxFilterRadius = 10.0f;

	// Cascade blend region as fraction of each cascade's depth range
	float CascadeBlendFactor = 0.3f;

	// Distance-based CSM/SDF split
	// CSM is used when view-space distance < SDFTransitionDistance.
	// SDF is used beyond. SDFTransitionRange controls the smooth fade width.
	float SDFTransitionDistance = 30.0f;
	float SDFTransitionRange = 5.0f;

	float _pad[1];
};

struct alignas(16) SDFShadowUniform
{
	vec4 LightDirection;   // w: softness factor
	vec4 VolumeResolution; // xyz: resolution, w: max march distance
	int MaxSteps;
	float MinDistance;
	float MaxDistance;
	float ShadowSoftness;
	float PaddingFactor;
	
	// Distance-based CSM/SDF split (same as ShadowUniform)
	float SDFTransitionDistance;
	float SDFTransitionRange;
	
	float _pad[1];
};

struct alignas(16) DFAOUniform
{
	vec4  VolumeResolution; // xyz: SDF volume resolution, w: unused
	int   NumSamples;       // AO ray cone sample count
	float MaxDistance;
	float Intensity;
	float StepScale;        // ray march step scale
	float PaddingFactor;    // SDF volume padding (matches SDFGenerator)
	float MinAO;            // minimum AO brightness (0 = fully dark allowed)
	float _pad[2];
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

struct alignas(16) GPUObjectData
{
	glm::vec4 boundingSphere;  // xyz: center, w: radius
	uint32_t transformIndex;
	uint32_t drawCommandIndex;
};

struct alignas(16) GPUCullData
{
	glm::mat4 view;
	glm::mat4 proj;
	glm::vec4 frustumPlanes[6];
	glm::vec2 screenSize;
	uint32_t drawCount;
	uint32_t hiZMipLevels;
	uint32_t enableOcclusionCulling;
};