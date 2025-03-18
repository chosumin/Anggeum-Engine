#version 450

#include "lighting.h"
#include "common.glsl"

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec4 worldPos;
layout(location = 2) in vec3 worldNormal;
layout(location = 3) in vec2 uv;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D basemap;

layout(binding = 2) uniform sampler2D normalmap;

layout(binding = 3) uniform sampler2D metallicRoughnessmap;

layout(binding = 4) uniform sampler2D shadowmap;

layout(binding = 5) uniform ShadowUniform
{
	mat4 projection;
} shadow;

layout(binding = 6) uniform PBR
{
    vec4 albedo;
    float metallic;
    float roughness;
    float ao;
} pbr;

layout(binding = 7) uniform LightInfo
{
	Light directionalLight;
} lightInfo;

//float CalculateShadow()
//{
//	vec4 projectedCoord = shadow.projection * worldPos;
//
//    projectedCoord /= projectedCoord.w;
//
//    projectedCoord.xy = 0.5 * projectedCoord.xy + 0.5;
//
//	float lightDepth = texture(shadowmap, projectedCoord.xy).r;
//
//	return lightDepth.r > projectedCoord.z ? 1.0f : 0.0f;
//}

void main()
{
	vec3 N = normalize(worldNormal);
	vec3 V = vec3(normalize(camera.view[3] - worldPos));

	//apply_directional_light(
	//float light = 1.0f * CalculateShadow();
	
	vec4 base = texture(basemap, uv);
	//outColor = vec4(base.rgb * light, base.a);
	outColor = vec4(base.rgb, base.a);
}