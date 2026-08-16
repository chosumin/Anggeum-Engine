#version 450

#include "common.glsl"
#include "terrainCommon.glsl"

// One instance = one visible patch from the GPU patch culling pass.
layout(set = 0, binding = 1) readonly buffer PatchList
{
    uvec4 patches[]; // x = packed node coord, y = slot, z = patch idx, w = deltas
} patchList;

layout(set = 0, binding = 2) uniform sampler2D heightAtlas;

layout(set = 0, binding = 5) uniform TerrainParamsUniform
{
    TerrainParams params;
};

layout(location = 0) out vec2 outTileUV;       // node-relative, for the color apron math
layout(location = 1) flat out uvec2 outColorOrigin;
layout(location = 2) flat out uint outLod;

const uint PATCH_VERTS = TERRAIN_PATCH_QUADS + 1u; // 17

void main()
{
    uvec4 patchEntry = patchList.patches[gl_InstanceIndex];
    uint lod = TerrainUnpackLod(patchEntry.x);
    uvec2 nodeCoord = TerrainUnpackCoord(patchEntry.x);
    uint slot = patchEntry.y;
    uvec2 patchXY = uvec2(patchEntry.z % TERRAIN_PATCHES_PER_EDGE,
        patchEntry.z / TERRAIN_PATCHES_PER_EDGE);

    uint vx = gl_VertexIndex % PATCH_VERTS;
    uint vy = gl_VertexIndex / PATCH_VERTS;

    // Position within the NODE's 129-texel grid: height texels sit exactly
    // on grid vertices, so this fetch is exact (no filtering).
    uvec2 nodeTexel = patchXY * TERRAIN_PATCH_QUADS + uvec2(vx, vy); // 0..128
    uvec2 slotOrigin = uvec2(slot % uint(params.atlasInfo.x),
        slot / uint(params.atlasInfo.x));
    uvec2 heightOrigin = slotOrigin * uint(params.atlasInfo.y);

    float height01 = texelFetch(heightAtlas,
        ivec2(heightOrigin + nodeTexel), 0).r;
    float height = mix(params.heightMinMaxInvAtlas.x,
        params.heightMinMaxInvAtlas.y, height01);

    float nodeSize = TerrainNodeSizeAt(params.worldParams.z,
        uint(params.worldParams.w), lod);
    vec2 local = vec2(nodeTexel) / float(TERRAIN_NODE_QUADS);
    vec3 world = vec3(
        params.worldParams.x + (float(nodeCoord.x) + local.x) * nodeSize,
        height,
        params.worldParams.y + (float(nodeCoord.y) + local.y) * nodeSize);

    outTileUV = local;
    outColorOrigin = slotOrigin * uint(params.atlasInfo.z);
    outLod = lod;

    gl_Position = camera.proj * camera.view * vec4(world, 1.0);
}
