#version 450

#include "common.glsl"

// No vertex inputs: the patch grid is derived from gl_VertexIndex and the
// per-node instance data. Mirrors TerrainNodeInstance in TerrainConfig.h.
struct NodeInstance
{
    vec2 originXZ;
    float sizeMeters;
    uint lod;
    uvec2 heightTexelOrigin;
    uvec2 colorTexelOrigin;
};

layout(set = 0, binding = 1) buffer readonly NodeInstances
{
    NodeInstance instances[];
} nodeInstances;

layout(set = 0, binding = 2) uniform sampler2D heightAtlas;

layout(set = 0, binding = 5) uniform TerrainParamsUniform
{
    vec4 heightMinMaxInvAtlas; // x = min, y = max, zw = 1 / heightAtlasExtent
    vec4 invColorAtlasBorder;  // xy = 1 / colorAtlasExtent, z = borderTexels
    vec4 sunDirection;         // xyz = direction, w = ambient
    ivec4 debugMode;
} params;

layout(location = 0) out vec2 outTileUV;
layout(location = 1) flat out uvec2 outColorOrigin;
layout(location = 2) flat out uint outLod;

const uint GRID_QUADS = 128u;
const uint GRID_VERTICES = GRID_QUADS + 1u;

void main()
{
    uint vx = gl_VertexIndex % GRID_VERTICES;
    uint vy = gl_VertexIndex / GRID_VERTICES;
    NodeInstance node = nodeInstances.instances[gl_InstanceIndex];

    // Height texels sit exactly on grid vertices: fetch, no filtering.
    float height01 = texelFetch(heightAtlas,
        ivec2(node.heightTexelOrigin) + ivec2(vx, vy), 0).r;
    float height = mix(params.heightMinMaxInvAtlas.x,
        params.heightMinMaxInvAtlas.y, height01);

    vec2 local = vec2(vx, vy) / float(GRID_QUADS);
    vec3 world = vec3(
        node.originXZ.x + local.x * node.sizeMeters,
        height,
        node.originXZ.y + local.y * node.sizeMeters);

    outTileUV = local;
    outColorOrigin = node.colorTexelOrigin;
    outLod = node.lod;

    gl_Position = camera.proj * camera.view * vec4(world, 1.0);
}
