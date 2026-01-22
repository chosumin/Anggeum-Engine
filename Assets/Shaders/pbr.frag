#version 450

// Source: https://learnopengl.com/PBR/Theory (Theory, Lighting and IBL sections)

#include "lighting.h"
#include "common.glsl"
#include "pbr.glsl"

layout(location = 0) in vec4 worldPos;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 1) uniform PBR
{
    vec4 albedo;
    float metallic;
    float roughness;
    float ao;
	int albedoTextureSet;
	int metallicTextureSet;
	int roughnessTextureSet;
	int occlusionTextureSet;
	int debugMode;
} pbr;

layout(set = 1, binding = 2) uniform sampler2D basemap;
layout(set = 1, binding = 3) uniform sampler2D normalmap;
layout(set = 1, binding = 4) uniform sampler2D metallicRoughnessmap;

layout(set = 0, binding = 3) uniform sampler2D shadowmap;

layout(set = 0, binding = 4) uniform ShadowUniform
{
	mat4 projection;
} shadow;

layout(set = 0, binding = 5) uniform Lights 
{
	Light lights[MAX_FORWARD_LIGHT_COUNT];
	uint count;
} lights;

layout(set = 0, binding = 6) buffer readonly TileLightVisiblities
{
    LightVisiblity lightVisiblities[];
};

layout(set = 0, binding = 7) uniform samplerCube irradiancemap;
layout(set = 0, binding = 8) uniform samplerCube prefiltermap;
layout(set = 0, binding = 9) uniform sampler2D brdfLut;

layout(std140, push_constant) uniform TileInfo
{
	ivec2 viewportSize;
	ivec2 tileNums;
} tileInfo;

vec3 Normal()
{
	vec3 dx = dFdx(worldPos.xyz);
	vec3 dy = dFdy(worldPos.xyz);
	vec3 st1 = dFdx(vec3(uv, 0.0));
	vec3 st2 = dFdy(vec3(uv, 0.0));
	vec3 T = (st2.t * dx - st1.t * dy) / (st1.s * st2.t - st2.s * st1.t);
	vec3 N = normalize(worldNormal);
	T = normalize(T - N * dot(N, T));
	vec3 B = normalize(cross(N, T));
	mat3 TBN = mat3(T, B, N);

	vec3 n = texture(normalmap, uv).rgb;

	return normalize(TBN * (2.0 * n - 1.0));
}

void main()
{
	vec4 albedo = vec4(1.0);
	float ao = 1.0;
	float roughness = 0.0;
	float metallic = 0.0;

	if (pbr.albedoTextureSet == 1)
	{
		albedo = SRGBtoLINEAR(texture(basemap, uv));
	}
	else
		albedo = pbr.albedo;

	vec4 metallicRoughness = vec4(0.0);
	if (pbr.metallicTextureSet == 1)
	{
		metallicRoughness = texture(metallicRoughnessmap, uv);
		metallic = metallicRoughness.b;
	}
	else
	{
		metallic = pbr.metallic;
	}

	if (pbr.roughnessTextureSet == 1)
	{
		roughness = metallicRoughness.g;
	}
	else
		roughness = pbr.roughness;

	if (pbr.occlusionTextureSet == 1)
	{
		//fixme : hardcoded. needs occlusion mapping
		ao = 1;
	}
	else
		ao = pbr.ao;

	vec3 N = normalize(Normal());
    vec3 V = normalize(camera.pos - worldPos.xyz);
	vec3 R = reflect(-V, N);

    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo.rgb, metallic);
	           
    // reflectance equation
    vec3 Lo = vec3(0.0);

	ivec2 tileId = ivec2(gl_FragCoord.xy / TILE_SIZE);
	uint tileIndex = tileId.y * tileInfo.tileNums.x + tileId.x;
	uint tileLightCount = lightVisiblities[tileIndex].count;
    for(int i = 0; i < tileLightCount; ++i) 
    {
		uint index = lightVisiblities[tileIndex].lightIndices[i];

        // calculate per-light radiance
        vec3 L = normalize(GetLightDirection(lights.lights[index], worldPos.xyz));
        vec3 H = normalize(V + L);
        vec3 radiance = ApplyLight(lights.lights[index], worldPos.xyz, N);        
        
        // cook-torrance brdf
        float NDF = DistributionGGX(N, H, roughness);        
        float G = GeometrySmith(N, V, L, roughness);      
        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);       
        
        vec3 kS = F;
        vec3 kD = 1.0 - kS;
        kD *= 1.0 - metallic;	  
        
        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular = numerator / denominator;  
            
        // add to outgoing radiance Lo             
        Lo += (kD * albedo.rgb / PI + specular) * radiance; 
    }   
  
	// ambient lighting
	vec3 kS = FresnelSchlick(max(dot(N, V), 0.0), F0);
	vec3 kD = 1.0 - kS;
	kD *= 1.0 - metallic;

	vec3 irradiance = SRGBtoLINEAR(texture(irradiancemap, N)).rgb;
	vec3 diffuse = irradiance * albedo.rgb;
	
	// split-sum approximation to get the IBL specular part.
	const float MAX_REFLECTION_LOD = 4.0;
	vec3 prefilteredColor = SRGBtoLINEAR(textureLod(prefiltermap, R, roughness * MAX_REFLECTION_LOD)).rgb;
	vec2 brdf = texture(brdfLut, vec2(max(dot(N, V), 0.0), roughness)).rg;
	vec3 specular = prefilteredColor * (brdf.x * kS + brdf.y);

	vec3 ambient = (kD * diffuse + specular) * ao;

	if(pbr.debugMode == 1)
	{
		float intensity = float(lightVisiblities[tileIndex].count) / 64;
		outColor = vec4(intensity, intensity, intensity, 1.0);
		return;
	}

    vec3 color = ambient + Lo;
	
	// tonemapping
    color = color / (color + vec3(1.0));

    outColor = vec4(color, 1.0);
}