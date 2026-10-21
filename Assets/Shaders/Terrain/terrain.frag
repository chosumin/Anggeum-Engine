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

// The shading cache: the near blend rendered per page by
// terrainPageRender.comp, and the per-level page table windows around the
// camera (toroidal: entry = absolute page coordinate modulo the window).
layout(set = 0, binding = 16) uniform sampler2D cacheAlbedo;
layout(set = 0, binding = 17) uniform sampler2D cacheNormal;
layout(set = 0, binding = 18) uniform TerrainPageTable
{
    ivec4 windowOrigin[TERRAIN_CACHE_MAX_LEVELS]; // xy = page coord of the window's min corner
    uvec4 entries[TERRAIN_CACHE_MAX_LEVELS * TERRAIN_CACHE_WINDOW * TERRAIN_CACHE_WINDOW / 4u];
} pageTable;

layout(set = 2, binding = 0) uniform sampler2D bindlessTextures2D[];
layout(set = 2, binding = 1) uniform samplerCube bindlessTexturesCube[];

#include "terrainShading.glsl"

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

const uint CACHE_LEVEL_NONE = 0xffu; // debug: the per-pixel path shaded this fragment

// Pipeline variant without the per-pixel blend: its register footprint
// costs every fragment occupancy even when all of them hit the cache, and
// with coarse pages rendered first a miss at every level is rare (startup,
// teleports), so that variant shows the far atlases there instead.
layout(constant_id = 0) const bool CACHE_ONLY = false;

uint CachePageSlot(uint level, ivec2 page)
{
    const int W = int(TERRAIN_CACHE_WINDOW);
    ivec2 rel = page - pageTable.windowOrigin[level].xy;
    if (any(lessThan(rel, ivec2(0))) || any(greaterThanEqual(rel, ivec2(W))))
        return TERRAIN_PAGE_EMPTY;

    ivec2 wrapped = ((page % W) + W) % W;
    uint i = level * uint(W * W) + uint(wrapped.y * W + wrapped.x);
    return pageTable.entries[i >> 2u][i & 3u];
}

// The cache has no mips (the levels are the mips), so the sampler's
// anisotropy acts within the page: explicit gradients keep it on inside the
// level walk's non-uniform control flow.
NearSurface SampleCachePage(uint level, uint slot, vec2 pagePos, vec2 dWdx, vec2 dWdy)
{
    vec2 uv = TerrainCacheTexel(params, slot, pagePos - floor(pagePos)) * params.cacheLevels.z;
    float duvdW = params.cacheInfo.y / TerrainCachePageSize(params, level) * params.cacheLevels.z;
    vec4 albedo = textureGrad(cacheAlbedo, uv, dWdx * duvdW, dWdy * duvdW);
    vec4 packed = textureGrad(cacheNormal, uv, dWdx * duvdW, dWdy * duvdW);
    vec2 xz = packed.rg * 2.0 - 1.0;

    NearSurface s;
    s.albedo = albedo.rgb;
    s.normal = vec3(xz.x, sqrt(max(1.0 - dot(xz, xz), 0.0)), xz.y);
    s.roughness = packed.b;
    s.idTint = vec3(0.0);
    return s;
}

// The near surface from the cache: the page level the pixel footprint asks
// for (texel size = finest * 2^level, chosen like an anisotropic mip: the
// minor axis, unless the major axis exceeds the anisotropy ratio), walking
// coarser until a page is resident; trilinear blends in the next coarser
// level. False when no level covers the point, which only happens while
// pages are still rendering.
bool SampleShadingCache(vec2 worldXZ, vec2 dWdx, vec2 dWdy, out NearSurface s, out uint level)
{
    const float MAX_ANISOTROPY = 16.0; // the sampler uses the device maximum
    uint levelCount = uint(params.cacheLevels.y);
    float finestTexel = params.cacheLevels.x / params.cacheInfo.y;
    float lenX = length(dWdx);
    float lenY = length(dWdy);
    float footprint = max(min(lenX, lenY), max(lenX, lenY) / MAX_ANISOTROPY);
    float levelF = clamp(log2(max(footprint / finestTexel, 1.0)), 0.0, float(levelCount - 1u));
    uint wanted = uint(levelF);

    uint slot = TERRAIN_PAGE_EMPTY;
    vec2 pagePos;
    for (level = wanted; level < levelCount; ++level)
    {
        pagePos = (worldXZ - params.worldParams.xy) / TerrainCachePageSize(params, level);
        slot = CachePageSlot(level, ivec2(floor(pagePos)));
        if (slot != TERRAIN_PAGE_EMPTY)
            break;
    }
    if (slot == TERRAIN_PAGE_EMPTY)
        return false;

    s = SampleCachePage(level, slot, pagePos, dWdx, dWdy);

    float t = levelF - float(level); // > 0 only when the wanted level itself was hit
    if (params.cacheLevels.w != 0.0 && t > 0.0 && level + 1u < levelCount)
    {
        vec2 coarsePos = pagePos * 0.5;
        uint coarseSlot = CachePageSlot(level + 1u, ivec2(floor(coarsePos)));
        if (coarseSlot != TERRAIN_PAGE_EMPTY)
        {
            NearSurface c = SampleCachePage(level + 1u, coarseSlot, coarsePos, dWdx, dWdy);
            s.albedo = mix(s.albedo, c.albedo, t);
            s.normal = normalize(mix(s.normal, c.normal, t));
            s.roughness = mix(s.roughness, c.roughness, t);
        }
    }
    return true;
}

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

    int mode = params.debugMode.x;

    // Debug views only: the nearest texel's ID (far side of Blend weights too).
    uint nearestId = 0u;
    if (mode == 6 || mode == 8)
        nearestId = texelFetch(splatAtlas,
            TerrainSplatTexel(inPatchEntry.y, inTileUV, params), 0).r & TERRAIN_SPLAT_ID_MASK;
    vec3 idTint = MaterialTint(nearestId);

    // Near: detail blend over the atlases; far: the atlases alone.
    vec2 dWdx = dFdx(inWorldPos.xz);
    vec2 dWdy = dFdy(inWorldPos.xz);
    float viewDistance = length(camera.pos - inWorldPos);
    float nearWeight = 1.0 - smoothstep(params.detailBlend.x,
        params.detailBlend.x + params.detailBlend.y, viewDistance);
    uint cacheLevel = CACHE_LEVEL_NONE;
    if (nearWeight > 0.0)
    {
        // The cache holds the blend already; the per-pixel path covers pages
        // still rendering and the blend-weight view (no tint in the cache).
        NearSurface near;
        bool cached = params.cacheInfo.x != 0.0 && mode != 8
            && SampleShadingCache(inWorldPos.xz, dWdx, dWdy, near, cacheLevel);
        if (!cached)
        {
            cacheLevel = CACHE_LEVEL_NONE;
            if (CACHE_ONLY)
            {
                near.albedo = albedo;
                near.normal = vec3(0.0, 1.0, 0.0);
                near.roughness = roughness;
                near.idTint = idTint;
            }
            else
                near = ShadeNear(inPatchEntry.y, inTileUV, inWorldPos.xz, dWdx, dWdy);
        }
        albedo = mix(albedo, near.albedo, nearWeight);
        roughness = mix(roughness, near.roughness, nearWeight);
        N = normalize(mix(N, PerturbNormal(N, near.normal), nearWeight));
        idTint = mix(idTint, near.idTint, nearWeight);
    }

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
    else if (mode == 6)
        color = MaterialTint(nearestId);
    else if (mode == 7)
        color = albedo;
    else if (mode == 8)
        color = idTint;
    else if (mode == 9)
        color = vec3(nearWeight);
    else if (mode == 10)
    {
        // Cache level tint with page borders; magenta = per-pixel fallback,
        // grey = far (no near blend at all).
        if (cacheLevel == CACHE_LEVEL_NONE)
            color = nearWeight > 0.0 ? vec3(1.0, 0.0, 1.0) : vec3(0.3);
        else
        {
            vec2 pagePos = (inWorldPos.xz - params.worldParams.xy)
                / TerrainCachePageSize(params, cacheLevel);
            vec2 edge = abs(fract(pagePos) - 0.5);
            float line = step(0.49, max(edge.x, edge.y));
            color = mix(LOD_COLORS[min(cacheLevel, 5u)] * (0.5 + 0.5 * albedo), vec3(1.0), line);
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
