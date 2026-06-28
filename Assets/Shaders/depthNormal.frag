#version 450
#extension GL_EXT_nonuniform_qualifier : require

#define GPU_DRIVEN_RENDERING 1

#include "common.glsl"

layout(location = 0) in vec4 inWorldPos;
layout(location = 1) in vec3 inWorldNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) flat in uint inDrawID;

// layout(location = 0) -> color attachment [0] (RT_MAIN_NORMAL)
layout(location = 0) out vec4 outNormal;

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

// Set 2: Bindless texture arrays (shared with lit.frag)
layout(set = 2, binding = 0) uniform sampler2D bindlessTextures2D[];

// Reconstruct a world-space normal from the normal map using screen-space
// derivatives, identical to lit.frag's Normal() since vertices carry no tangent.
vec3 ApplyNormalMap(uint normalmapIndex)
{
    vec3 dx  = dFdx(inWorldPos.xyz);
    vec3 dy  = dFdy(inWorldPos.xyz);
    vec3 st1 = dFdx(vec3(inUV, 0.0));
    vec3 st2 = dFdy(vec3(inUV, 0.0));

    vec3 T = (st2.t * dx - st1.t * dy) / (st1.s * st2.t - st2.s * st1.t);
    vec3 N = normalize(inWorldNormal);
    T = normalize(T - N * dot(N, T));
    vec3 B = normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);

    vec3 n = texture(bindlessTextures2D[nonuniformEXT(normalmapIndex)], inUV).rgb;

    return normalize(TBN * (2.0 * n - 1.0));
}

void main()
{
    uint materialIndex = materialIndices.materialIndices[inDrawID];
    PBR pbr = pbrBuffer.materials[materialIndex];

    // Start from the interpolated geometric normal, then apply the normal map
    // when the material provides one (normalmapIndex != 0).
    vec3 N = normalize(inWorldNormal);
    if (pbr.normalmapIndex != 0u)
    {
        N = ApplyNormalMap(pbr.normalmapIndex);
    }

    // CACAO expects a [0,1] packed normal (it unpacks with N * 2 - 1).
    //vec3 packedNormal = N * 0.5 + 0.5;
    vec3 packedNormal = N * 0.5 + 0.5;

    outNormal = vec4(packedNormal, 1.0);
}