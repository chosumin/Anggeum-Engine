#version 450
#extension GL_EXT_nonuniform_qualifier : require

#define GPU_DRIVEN_RENDERING 1

// Source: https://learnopengl.com/PBR/Theory (Theory, Lighting and IBL sections)

#include "lighting.h"
#include "common.glsl"
#include "pbr.glsl"
#include "shadow.glsl"

layout(location = 0) in vec4 worldPos;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 uv;

#ifdef GPU_DRIVEN_RENDERING
layout(location = 3) flat in uint drawID;
#endif

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 3) uniform GI
{
	uint irradiancemapIndex;
	uint prefiltermapIndex;
	uint brdfLutIndex;
} gi;

layout(set = 0, binding = 4) uniform CascadeShadowUBO {
    mat4  viewProjection[SHADOW_MAP_CASCADE_COUNT];
    float splitDepth[SHADOW_MAP_CASCADE_COUNT];
    uint  cascadeCount;
	float lightSize;
    float minFilterRadius;
    float maxFilterRadius;
    float cascadeBlendFactor;
	float sdfTransitionDistance;
	float sdfTransitionRange;
} csm;

layout(set = 0, binding = 5) uniform Lights 
{
	Light lights[MAX_FORWARD_LIGHT_COUNT];
	uint count;
} lights;

layout(set = 0, binding = 6) buffer readonly TileLightVisiblities
{
    LightVisiblity lightVisiblities[];
};

layout(set = 0, binding = 7) uniform sampler2DArray shadowMap;

layout(set = 0, binding = 10) uniform sampler2D sdfShadowMap;
layout(set = 0, binding = 11) uniform sampler2D aoMap;

#ifdef GPU_DRIVEN_RENDERING
struct PBR
{
	vec4 albedo;
    float metallic;
    float roughness;
    float ao;
	int flags;

	int albedoTextureSet;
	int metallicTextureSet;
	int roughnessTextureSet;
	int occlusionTextureSet;
	int debugMode;

	uint basemapIndex;              // Material texture 0
	uint normalmapIndex;            // Material texture 1
	uint metallicRoughnessmapIndex; // Material texture 2

	vec3 padding;
};

layout(set = 0, binding = 8) uniform PBRBuffer
{
    PBR materials[256];
} pbrBuffer;

layout(set = 0, binding = 9) readonly buffer MaterialIndexBuffer {
    uint materialIndices[];
} materialIndices;
#else
// Set 1: Material properties with texture indices
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

	uint basemapIndex;              // Material texture 0
	uint normalmapIndex;            // Material texture 1
	uint metallicRoughnessmapIndex; // Material texture 2
} pbr;
#endif

// Set 2: Bindless texture arrays
layout(set = 2, binding = 0) uniform sampler2D bindlessTextures2D[];
layout(set = 2, binding = 1) uniform samplerCube bindlessTexturesCube[];

layout(std140, push_constant) uniform TileInfo
{
	ivec2 viewportSize;
	ivec2 tileNums;
} tileInfo;

void main()
{
#ifdef GPU_DRIVEN_RENDERING
	uint materialIndex = materialIndices.materialIndices[drawID];
    PBR pbr = pbrBuffer.materials[materialIndex];
#endif

	vec4 albedo = vec4(1.0);
	float roughness = 0.0;
	float metallic = 0.0;

	if (pbr.albedoTextureSet == 1)
	{
		albedo = SRGBtoLINEAR(texture(bindlessTextures2D[nonuniformEXT(pbr.basemapIndex)], uv));
	}
	else
		albedo = pbr.albedo;

	vec4 metallicRoughness = vec4(0.0);
	if (pbr.metallicTextureSet == 1 || pbr.roughnessTextureSet == 1)
	{
		metallicRoughness = texture(bindlessTextures2D[nonuniformEXT(pbr.metallicRoughnessmapIndex)], uv);
	}

	if (pbr.metallicTextureSet == 1)
	{
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
	{
		roughness = pbr.roughness;
	}

	vec3 n = texture(bindlessTextures2D[nonuniformEXT(pbr.normalmapIndex)], uv).rgb;
	vec3 N = normalize(Normal(n, worldPos.xyz, worldNormal, uv));
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
  
	// Calculate shadow visibility using CSM with PCSS (1.0 = fully lit, 0.0 = fully shadowed)
	float viewDepth = (camera.view * worldPos).z;
	float csmVisibility = ShadowCalculation(shadowMap,
		csm.viewProjection, csm.splitDepth, csm.cascadeCount,
		csm.lightSize, csm.minFilterRadius, csm.maxFilterRadius,
		csm.cascadeBlendFactor,
		worldPos.xyz, viewDepth);

	vec2 screenUV = gl_FragCoord.xy / vec2(tileInfo.viewportSize);
	float sdfVisibility = SampleSDFShadow(sdfShadowMap, screenUV);

	// Distance-based split: near = CSM only, far = SDF only
	float visibility = CombineShadows(csmVisibility, sdfVisibility,
		viewDepth, csm.sdfTransitionDistance, csm.sdfTransitionRange);

	// Apply shadow to direct lighting only (ambient is unaffected)
	Lo *= visibility;

	// Sample AO: applied globally across the full screen to the ambient term
	float ao = texture(aoMap, screenUV).r;

	// ambient lighting
	vec3 kS = FresnelSchlick(max(dot(N, V), 0.0), F0);
	vec3 kD = 1.0 - kS;
	kD *= 1.0 - metallic;

	vec3 irradiance = texture(bindlessTexturesCube[nonuniformEXT(gi.irradiancemapIndex)], N).rgb;
	vec3 diffuse = irradiance * albedo.rgb;
	
	// split-sum approximation to get the IBL specular part.
	const float MAX_REFLECTION_LOD = 4.0;

	vec3 prefilteredColor = textureLod(bindlessTexturesCube[nonuniformEXT(gi.prefiltermapIndex)], R, roughness * MAX_REFLECTION_LOD).rgb;

	vec2 brdf = texture(bindlessTextures2D[nonuniformEXT(gi.brdfLutIndex)], vec2(max(dot(N, V), 0.0), roughness)).rg;
	vec3 specular = prefilteredColor * (brdf.x * kS + brdf.y);

	const float AO = 0.1; // Global AO factor to reduce ambient lighting
	vec3 ambient = (kD * diffuse * ao + specular) * AO;
    vec3 color = ambient + Lo;
	
	float exposure = 4.5;
	vec4 mapped = Tonemap(vec4(color, 1.0), exposure, 1.0);
	
    outColor = vec4(mapped.rgb, 1.0);
}