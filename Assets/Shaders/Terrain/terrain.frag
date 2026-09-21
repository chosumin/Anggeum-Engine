#version 450

#include "terrainCommon.glsl"

layout(set = 0, binding = 3) uniform sampler2D normalAtlas;
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

layout(location = 0) in vec2 inTileUV;
layout(location = 1) flat in uvec2 inColorOrigin;
layout(location = 2) flat in uint inLod;
layout(location = 3) flat in uvec4 inPatchEntry;

layout(location = 0) out vec4 outColor;

const float GRID_QUADS = float(TERRAIN_NODE_QUADS);

const vec3 LOD_COLORS[6] = vec3[](
    vec3(1.0, 0.2, 0.2), vec3(1.0, 0.6, 0.1), vec3(0.9, 0.9, 0.1),
    vec3(0.2, 0.9, 0.2), vec3(0.2, 0.5, 1.0), vec3(0.7, 0.3, 0.9));

void main()
{
    // The apron makes bilinear filtering seamless right up to node edges.
    vec2 texel = vec2(inColorOrigin) + params.invColorAtlasBorder.z
        + inTileUV * GRID_QUADS;
    vec2 uv = texel * params.invColorAtlasBorder.xy;

    vec3 normal = normalize(texture(normalAtlas, uv).xyz * 2.0 - 1.0);
    vec3 albedo = texture(albedoAtlas, uv).rgb;

    float diffuse = max(dot(normal, normalize(params.sunDirection.xyz)), 0.0);
    vec3 lit = albedo * (diffuse + params.sunDirection.w);

    int mode = params.debugMode.x;
    if (mode == 1)
        lit = mix(lit, LOD_COLORS[min(inLod, 5u)], 0.5);
    else if (mode == 2)
        lit = normal * 0.5 + 0.5;
    else if (mode == 3)
    {
        vec2 grid = fract(inTileUV * GRID_QUADS);
        float line = step(0.9, max(grid.x, grid.y));
        lit = mix(vec3(0.2), vec3(1.0), line);
    }

    // Debug mode: culled patches are drawn tinted by the test that dropped them.
    uint cullBits = inPatchEntry.z & (TERRAIN_CULL_FRUSTUM | TERRAIN_CULL_OCCLUDED);
    if (cullBits == TERRAIN_CULL_FRUSTUM)
        lit = mix(lit, vec3(1.0, 0.0, 0.0), 0.6);
    else if (cullBits == TERRAIN_CULL_OCCLUDED)
        lit = mix(lit, vec3(0.0, 0.2, 1.0), 0.6);

    // debugMode.yz = mouse pixel, .w = pick on
    if (params.debugMode.w != 0 && ivec2(gl_FragCoord.xy) == params.debugMode.yz)
        pick.entry = uvec4(inPatchEntry.xy, inPatchEntry.z | 0x80000000u, inPatchEntry.w);

    outColor = vec4(lit, 1.0);
}
