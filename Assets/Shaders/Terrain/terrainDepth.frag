#version 450

// Terrain's depth prepass fragment: pairs with terrain.vert (whose lod output
// it simply does not consume) and writes the packed world normal the resolve/
// AO chain expects, matching depthNormal.frag's encoding (N * 0.5 + 0.5).

#include "terrainCommon.glsl"

layout(set = 0, binding = 3) uniform sampler2D normalAtlas;

layout(set = 0, binding = 5) uniform TerrainParamsUniform
{
    TerrainParams params;
};

layout(location = 0) in vec2 inTileUV;
layout(location = 1) flat in uvec2 inColorOrigin;

layout(location = 0) out vec4 outNormal;

const float GRID_QUADS = float(TERRAIN_NODE_QUADS);

void main()
{
    // Same apron math as terrain.frag: seamless bilinear up to node edges.
    vec2 texel = vec2(inColorOrigin) + params.invColorAtlasBorder.z
        + inTileUV * GRID_QUADS;
    vec2 uv = texel * params.invColorAtlasBorder.xy;

    vec3 normal = normalize(texture(normalAtlas, uv).xyz * 2.0 - 1.0);
    outNormal = vec4(normal * 0.5 + 0.5, 1.0);
}
