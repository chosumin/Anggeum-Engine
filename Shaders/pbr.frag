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
	int albedoTextureSet;
	int metallicTextureSet;
	int roughnessTextureSet;
	int occlusionTextureSet;
	int debugMode;
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
	float a = roughness * roughness;
	float a2 = a * a;
	float NdotH = max(dot(N, H), 0.0);
	float NdotH2 = NdotH * NdotH;

	float denom = NdotH2 * (a2 - 1.0) + 1.0;
	denom = PI * denom * denom;

	return a2 / denom;
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
	float NdotV = max(dot(N, V), 0.0000001);
	float NdotL = max(dot(N, L), 0.0000001);
	float ggx2 = GeomertySchlickGGX(NdotV, roughness);
	float ggx1 = GeomertySchlickGGX(NdotL, roughness);

	return ggx1 * ggx2;
}

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
		albedo = texture(basemap, uv);
		albedo.rgb = pow(albedo.rgb, vec3(2.2));
	}
	else
		albedo = pow(pbr.albedo, vec4(2.2));

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
		ao = metallicRoughness.r;
	}
	else
		ao = pbr.ao;

	vec3 N = normalize(Normal());
    vec3 V = normalize(camera.pos - worldPos.xyz);

    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo.rgb, metallic);
	           
    // reflectance equation
    vec3 Lo = vec3(0.0);
    for(int i = 0; i < 1; ++i) 
    {
        // calculate per-light radiance
        vec3 L = normalize(-lightInfo.light.direction.xyz);
        vec3 H = normalize(V + L);
        vec3 radiance     = lightInfo.light.color.w * lightInfo.light.color.rgb;        
        
        // cook-torrance brdf
        float NDF = DistributionGGX(N, H, roughness);        
        float G   = GeometrySmith(N, V, L, roughness);      
        vec3 F    = FresnelSchlick(max(dot(H, V), 0.0), F0);       
        
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;	  
        
        vec3 numerator    = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular     = numerator / denominator;  
            
        // add to outgoing radiance Lo
        float NdotL = max(dot(N, L), 0.0);                
        Lo += (kD * albedo.rgb / PI + specular) * radiance * NdotL; 
    }   
  
    vec3 ambient = vec3(0.03) * albedo.rgb * ao;
    vec3 color = ambient + Lo;
	
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));  
   
	if (pbr.debugMode == 1)
	{
		vec3 L = normalize(-lightInfo.light.direction.xyz);
        vec3 H = normalize(V + L);
		float NDF = DistributionGGX(N, H, roughness);
		outColor = vec4(NDF, NDF, NDF, 1.0);
		return;
	}
	else if (pbr.debugMode == 2)
	{
		vec3 L = normalize(-lightInfo.light.direction.xyz);
        vec3 H = normalize(V + L);
		float G   = GeometrySmith(N, V, L, roughness);  
		outColor = vec4(G, G, G, 1.0);
		return;
	}
	else if (pbr.debugMode == 3)
	{
		vec3 L = normalize(-lightInfo.light.direction.xyz);
        vec3 H = normalize(V + L);
        vec3 F    = FresnelSchlick(max(dot(H, V), 0.0), F0);       
		outColor = vec4(F, 1.0);
		return;
	}

    outColor = vec4(color, 1.0);
}