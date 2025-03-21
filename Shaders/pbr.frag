#version 450

// Source: https://learnopengl.com/PBR/Theory (Theory, Lighting and IBL sections)

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
	Light light;
} lightInfo;

float saturate(float t)
{
	return clamp(t, 0.0, 1.0f);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
	return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

//the normal distribution function
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
	float a = roughness;
	float a2 = a * a;
	float NdotH = max(dot(N, H), 0.0);
	float NdotH2 = NdotH * NdotH;

	float num = a2;
	float denom = (NdotH2 * (a2 - 1.0) + 1.0);
	denom = PI * denom * denom;

	return num / denom;
}

//the geometry function
float GeomertySchlickGGX(float NdotV, float roughness)
{
	float r = (roughness + 1.0);
	float k = (r * r) / 8.0;

	float num = NdotV;
	float denom = NdotV * (1.0 - k) + k;

	return num / denom;
}

//the geometry function for specular microfacet model
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float ggx2 = GeomertySchlickGGX(NdotV, roughness);
	float ggx1 = GeomertySchlickGGX(NdotL, roughness);

	return ggx1 * ggx2;
}

void main()
{
	vec4 albedo = texture(basemap, uv);

	vec4 metallicRoughness = texture(metallicRoughnessmap, uv);

	float ao = metallicRoughness.r;
	float roughness = metallicRoughness.g;
	float metallic = metallicRoughness.b;

	vec3 N = normalize(worldNormal);
	vec3 V = normalize(camera.view[3].xyz - worldPos.xyz);

	vec3 F0 = vec3(0.04); //simply assumption
	F0 = mix(F0, albedo.rgb, metallic); //interpolation between metallic and albedo

	vec3 Lo = vec3(0.0);

	for (uint i = 0; i < 1; ++i)
	{
		vec3 L = normalize(GetLightDirection(lightInfo.light, worldPos.xyz));
		vec3 H = normalize(V + L); //half vector

		vec3 radiance = ApplyLight(lightInfo.light, worldPos.xyz, N);

		//Cook-Torrance BRDF
		float NDF = DistributionGGX(N, H, roughness);
		float G = GeometrySmith(N, V, L, roughness);
		vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

		vec3 kS = F; //specular fraction
		vec3 kD = vec3(1.0) - kS; //diffuse fraction
		kD *= 1.0 - metallic;

		vec3 numerator = NDF * G * F;

		//Note the 0.0001 is for preventing a divide by zero.
		float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
		vec3 specular = numerator / denominator;

		Lo += (kD * albedo.rgb / PI + specular) * radiance;
	}

	vec3 ambient = vec3(0.03) * albedo.rgb * ao;
	vec3 color = ambient + Lo;

	//Gamma correction and HDR
	color = color / (color + vec3(1.0));
	color = pow(color, vec3(1.0 / 2.2));

	outColor = vec4(color, albedo.a);
}