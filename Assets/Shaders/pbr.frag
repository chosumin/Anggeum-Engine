#version 450

// Source: https://learnopengl.com/PBR/Theory (Theory, Lighting and IBL sections)

#include "lighting.h"
#include "common.glsl"
#include "pbr.glsl"

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
	int albedoTextureSet;
	int metallicTextureSet;
	int roughnessTextureSet;
	int occlusionTextureSet;
	int debugMode;
} pbr;

layout(binding = 7) uniform LightInfo lights;
layout(binding = 8) buffer readonly TileLightVisiblities
{
    LightVisiblity lightVisiblities[];
};

layout(binding = 9) uniform samplerCube irradiancemap;
layout(binding = 10) uniform samplerCube prefiltermap;
layout(binding = 11) uniform sampler2D brdfLut;

layout(push_constant) uniform TileInfo
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
        // calculate per-light radiance
        vec3 L = normalize(GetLightDirection(lights.lights[i], worldPos.xyz));
        vec3 H = normalize(V + L);
        vec3 radiance = ApplyLight(lights.lights[i], worldPos.xyz, N);        
        
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
		ambient = vec3(0.03) * albedo.rgb * ao;
	}

    vec3 color = ambient + Lo;
	
	// tonemapping
    color = color / (color + vec3(1.0));

    outColor = vec4(color, 1.0);
}