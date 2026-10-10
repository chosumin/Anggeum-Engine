#version 450
#extension GL_EXT_nonuniform_qualifier : require

#include "common.glsl"
#include "lighting.glsl"
#include "shadow.glsl"

#define GPU_DRIVEN_RENDERING 1

layout(location = 0) in vec4 worldPos;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 uv;

#ifdef GPU_DRIVEN_RENDERING
layout(location = 3) flat in uint inMaterialIndex;
#endif

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 3) uniform GI
{
	uint irradiancemapIndex;
	uint prefiltermapIndex;
	uint brdfLutIndex;
} gi;

layout(set = 0, binding = 4) uniform CascadeShadowUBO { CascadeShadowParams csm; };

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

// Tangent-space normal map to world space, with the tangent frame derived
// from screen-space derivatives.
vec3 Normal(vec3 normalMap, vec3 worldPos, vec3 worldNormal, vec2 uv)
{
	vec3 dx = dFdx(worldPos);
	vec3 dy = dFdy(worldPos);
	vec3 st1 = dFdx(vec3(uv, 0.0));
	vec3 st2 = dFdy(vec3(uv, 0.0));
	vec3 T = (st2.t * dx - st1.t * dy) / (st1.s * st2.t - st2.s * st1.t);
	vec3 N = normalize(worldNormal);
	T = normalize(T - N * dot(N, T));
	vec3 B = normalize(cross(N, T));
	mat3 TBN = mat3(T, B, N);

	return normalize(TBN * (2.0 * normalMap - 1.0));
}

void main()
{
#ifdef GPU_DRIVEN_RENDERING
	uint materialIndex = inMaterialIndex;
    PBR pbr = pbrBuffer.materials[materialIndex];
#endif

	vec4 albedo = vec4(1.0);
	float roughness = 0.0;
	float metallic = 0.0;

	if (pbr.albedoTextureSet == 1)
	{
		albedo = texture(bindlessTextures2D[nonuniformEXT(pbr.basemapIndex)], uv);
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
	
	Surface surface = MakeSurface(albedo.rgb, metallic, roughness, N, V);

    vec3 Lo = vec3(0.0);

	ivec2 tileId = ivec2(gl_FragCoord.xy / TILE_SIZE);
	uint tileIndex = tileId.y * tileInfo.tileNums.x + tileId.x;
	uint tileLightCount = lightVisiblities[tileIndex].count;
    for(int i = 0; i < tileLightCount; ++i)
    {
		uint index = lightVisiblities[tileIndex].lightIndices[i];
		Lo += DirectLighting(surface, lights.lights[index], worldPos.xyz);
    }

	// Shadow visibility (1.0 = fully lit, 0.0 = fully shadowed)
	vec2 screenUV = gl_FragCoord.xy / vec2(tileInfo.viewportSize);
	float visibility = ShadowVisibility(shadowMap, sdfShadowMap, csm, camera.view,
		worldPos.xyz, screenUV);

	// Apply shadow to direct lighting only (ambient is unaffected)
	Lo *= visibility;

	// Sample AO: applied globally across the full screen to the ambient term
	float ao = texture(aoMap, screenUV).r;

	vec3 ambient = AmbientLighting(surface,
		bindlessTexturesCube[nonuniformEXT(gi.irradiancemapIndex)],
		bindlessTexturesCube[nonuniformEXT(gi.prefiltermapIndex)],
		bindlessTextures2D[nonuniformEXT(gi.brdfLutIndex)],
		ao, 1.0);
    vec3 color = ambient + Lo;

	vec4 mapped = Tonemap(vec4(color, 1.0), FORWARD_EXPOSURE, 1.0);

    outColor = vec4(mapped.rgb, 1.0);
}