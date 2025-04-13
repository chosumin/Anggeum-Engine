#pragma once

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
	LightInfo Light;
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