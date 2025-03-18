#pragma once

struct VPBufferObject
{
	alignas(16) mat4 View;
	alignas(16) mat4 Perspective;
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
	LightInfo DirectionalLight;
};