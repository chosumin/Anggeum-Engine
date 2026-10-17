#version 450
#extension GL_EXT_nonuniform_qualifier : require

#include "common.glsl"
#include "lighting.glsl"
#include "shadow.glsl"
#include "terrainCommon.glsl"

layout(set = 0, binding = 3) uniform sampler2D normalAtlas; // see TerrainDecodeNormal
layout(set = 0, binding = 4) uniform sampler2D albedoAtlas;

layout(set = 0, binding = 5) uniform TerrainParamsUniform
{
    TerrainParams params;
};

// Only the surviving fragment may report itself as the pick.
layout(early_fragment_tests) in;

// The patch under the mouse, for the terrain debug GUI (host-visible).
layout(set = 0, binding = 6) buffer PickBuffer
{
    uvec4 entry; // patch list entry, z bit 31 = written
} pick;

layout(set = 0, binding = 7) uniform CascadeShadowUBO { CascadeShadowParams csm; };
layout(set = 0, binding = 8) uniform sampler2DArray shadowMap;
layout(set = 0, binding = 9) uniform sampler2D sdfShadowMap;

// The lighting inputs lit.frag reads, so terrain and meshes shade alike.
layout(set = 0, binding = 10) uniform GI
{
    uint irradiancemapIndex;
    uint prefiltermapIndex;
    uint brdfLutIndex;
} gi;

layout(set = 0, binding = 11) uniform Lights
{
    Light lights[MAX_FORWARD_LIGHT_COUNT];
    uint count;
} lights;

layout(set = 0, binding = 12) buffer readonly TileLightVisiblities
{
    LightVisiblity lightVisiblities[];
};

layout(set = 0, binding = 13) uniform sampler2D aoMap;

// Material IDs per splat texel, and the palette they index.
layout(set = 0, binding = 14) uniform usampler2D splatAtlas;
layout(set = 0, binding = 15) uniform TerrainMaterialPalette
{
    TerrainMaterial materials[TERRAIN_MAX_MATERIALS];
    uvec4 count; // x = material count
} palette;

layout(set = 2, binding = 0) uniform sampler2D bindlessTextures2D[];
layout(set = 2, binding = 1) uniform samplerCube bindlessTexturesCube[];

layout(location = 0) in vec2 inTileUV;
layout(location = 1) flat in uvec2 inColorOrigin;
layout(location = 2) flat in uint inLod;
layout(location = 3) flat in uvec4 inPatchEntry;
layout(location = 4) in vec3 inWorldPos;

layout(location = 0) out vec4 outColor;

const float GRID_QUADS = float(TERRAIN_NODE_QUADS);

const vec3 LOD_COLORS[6] = vec3[](
    vec3(1.0, 0.2, 0.2), vec3(1.0, 0.6, 0.1), vec3(0.9, 0.9, 0.1),
    vec3(0.2, 0.9, 0.2), vec3(0.2, 0.5, 1.0), vec3(0.7, 0.3, 0.9));

// One tint per palette slot; sized by the palette so a bigger palette
// fails to compile until this grows with it.
const vec3 MATERIAL_COLORS[TERRAIN_MAX_MATERIALS] = vec3[](
    vec3(0.2, 0.8, 0.2), vec3(0.6, 0.4, 0.2), vec3(0.5, 0.5, 0.5), vec3(1.0, 1.0, 1.0),
    vec3(1.0, 0.2, 0.2), vec3(0.2, 0.2, 1.0), vec3(1.0, 1.0, 0.2), vec3(1.0, 0.2, 1.0),
    vec3(0.2, 1.0, 1.0), vec3(1.0, 0.6, 0.0), vec3(0.5, 0.0, 0.8), vec3(0.0, 0.5, 0.3),
    vec3(0.8, 0.5, 0.5), vec3(0.3, 0.3, 0.0), vec3(0.6, 0.8, 1.0), vec3(0.1, 0.1, 0.1));

void main()
{
    // The apron makes bilinear filtering seamless right up to node edges.
    vec2 texel = vec2(inColorOrigin) + params.invColorAtlasBorder.z
        + inTileUV * GRID_QUADS;
    vec2 uv = texel * params.invColorAtlasBorder.xy;

    vec4 packedNormal = texture(normalAtlas, uv);

    vec3 albedo = texture(albedoAtlas, uv).rgb;
    float roughness = packedNormal.b;
    float bakedAO = packedNormal.a;

    vec3 N = TerrainDecodeNormal(packedNormal);
    vec3 V = normalize(camera.pos - inWorldPos);

    Surface surface = MakeSurface(albedo, 0.0, roughness, N, V); // dielectric

    vec2 screenUV = gl_FragCoord.xy / params.viewport.xy;
    ivec2 tileId = ivec2(gl_FragCoord.xy / TILE_SIZE);
    uint tileIndex = uint(tileId.y) * uint(params.viewport.z) + uint(tileId.x);
    uint tileLightCount = lightVisiblities[tileIndex].count;

    vec3 Lo = vec3(0.0);
    for (uint i = 0u; i < tileLightCount; ++i)
    {
        uint index = lightVisiblities[tileIndex].lightIndices[i];
        Lo += DirectLighting(surface, lights.lights[index], inWorldPos);
    }

    float visibility = ShadowVisibility(shadowMap, sdfShadowMap, csm, camera.view,
        inWorldPos, screenUV);
    Lo *= visibility;

    // Screen-space AO x baked horizon AO on the ambient diffuse
    float ao = texture(aoMap, screenUV).r * bakedAO;
    float specularOcclusion = SpecularOcclusion(max(dot(N, V), 0.0), bakedAO, roughness);

    vec3 ambient = AmbientLighting(surface,
        bindlessTexturesCube[nonuniformEXT(gi.irradiancemapIndex)],
        bindlessTexturesCube[nonuniformEXT(gi.prefiltermapIndex)],
        bindlessTextures2D[nonuniformEXT(gi.brdfLutIndex)],
        ao, specularOcclusion);

    vec3 color = Tonemap(vec4(ambient + Lo, 1.0), FORWARD_EXPOSURE, 1.0).rgb;

    // Debug views replace or tint the tonemapped color.
    int mode = params.debugMode.x;
    if (mode == 1)
        color = mix(color, LOD_COLORS[min(inLod, 5u)], 0.5);
    else if (mode == 2)
        color = N * 0.5 + 0.5;
    else if (mode == 3)
    {
        vec2 grid = fract(inTileUV * GRID_QUADS);
        float line = step(0.9, max(grid.x, grid.y));
        color = mix(vec3(0.2), vec3(1.0), line);
    }
    else if (mode == 4)
        color = vec3(roughness);
    else if (mode == 5)
        color = vec3(bakedAO);
    else if (mode == 6 || mode == 7)
    {
        // Nearest splat texel -> palette entry; the near-shading blend of the
        // four surrounding texels comes with Phase 3c.
        uint materialId = texelFetch(splatAtlas,
            TerrainSplatTexel(inPatchEntry.y, inTileUV, params), 0).r & TERRAIN_SPLAT_ID_MASK;
        if (mode == 6)
            color = MATERIAL_COLORS[min(materialId, TERRAIN_MAX_MATERIALS - 1u)];
        else
        {
            TerrainMaterial material = palette.materials[min(materialId, palette.count.x - 1u)];
            vec2 detailUV = TerrainDetailUV(material, inWorldPos.xz);
            vec3 detail = texture(bindlessTextures2D[nonuniformEXT(material.textures.x)], detailUV).rgb;
            color = Tonemap(vec4(detail * (ambient + Lo) / max(albedo, vec3(1e-3)), 1.0),
                FORWARD_EXPOSURE, 1.0).rgb;
        }
    }

    // Debug mode: culled patches are drawn tinted by the test that dropped them.
    uint cullBits = inPatchEntry.z & (TERRAIN_CULL_FRUSTUM | TERRAIN_CULL_OCCLUDED);
    if (cullBits == TERRAIN_CULL_FRUSTUM)
        color = mix(color, vec3(1.0, 0.0, 0.0), 0.6);
    else if (cullBits == TERRAIN_CULL_OCCLUDED)
        color = mix(color, vec3(0.0, 0.2, 1.0), 0.6);

    // debugMode.yz = mouse pixel, .w = pick on
    if (params.debugMode.w != 0 && ivec2(gl_FragCoord.xy) == params.debugMode.yz)
        pick.entry = uvec4(inPatchEntry.xy, inPatchEntry.z | 0x80000000u, inPatchEntry.w);

    outColor = vec4(color, 1.0);
}
