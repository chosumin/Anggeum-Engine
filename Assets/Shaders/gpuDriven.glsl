#ifndef GPU_DRIVEN_GLSL
#define GPU_DRIVEN_GLSL

// instanceData.drawCommandIndex of a freed entry: skip it.
const uint DEAD_DRAW = 0xFFFFFFFFu;

// Same layout as VkDrawIndexedIndirectCommand.
struct DrawIndexedIndirectCommand
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

// One instance of the draw set (RendererBatch instanceData).
struct InstanceData
{
    vec3 aabbMin;         // local
    uint transformIndex;
    vec3 aabbMax;         // local
    uint drawCommandIndex;
    uint materialIndex;
    uint padding[3];
};

// The instance's local AABB under its world transform, as a world AABB.
void InstanceWorldAABB(InstanceData inst, mat4 world, out vec3 worldMin, out vec3 worldMax)
{
    vec3 center = (inst.aabbMin + inst.aabbMax) * 0.5;
    vec3 extent = (inst.aabbMax - inst.aabbMin) * 0.5;

    vec3 worldCenter = (world * vec4(center, 1.0)).xyz;
    vec3 worldExtent = abs(world[0].xyz) * extent.x
        + abs(world[1].xyz) * extent.y
        + abs(world[2].xyz) * extent.z;

    worldMin = worldCenter - worldExtent;
    worldMax = worldCenter + worldExtent;
}

bool IsVisibleFrustum(vec4 frustumPlanes[6], vec3 aabbMin, vec3 aabbMax)
{
    for (int i = 0; i < 6; ++i)
    {
        vec3 normal = frustumPlanes[i].xyz;
        // The corner farthest along the plane normal decides.
        vec3 corner = mix(aabbMin, aabbMax, greaterThan(normal, vec3(0.0)));
        if (dot(normal, corner) + frustumPlanes[i].w < 0.0)
            return false;
    }
    return true;
}

bool IsOccluded(sampler2D hiZBuffer, mat4 viewProj, vec2 screenSize, uint hiZMipLevels,
    vec3 aabbMin, vec3 aabbMax)
{
    // Project the 8 corners into a screen rectangle and the nearest depth.
    vec2 uvMin = vec2(1.0);
    vec2 uvMax = vec2(0.0);
    float nearestDepth = 1.0;
    for (int i = 0; i < 8; ++i)
    {
        vec3 corner = mix(aabbMin, aabbMax,
            bvec3((i & 1) != 0, (i & 2) != 0, (i & 4) != 0));
        vec4 clip = viewProj * vec4(corner, 1.0);

        // A corner behind the near plane has no screen position: frustum
        // culling owns that case.
        if (clip.w <= 0.0)
            return false;

        vec3 ndc = clip.xyz / clip.w;
        vec2 uv = ndc.xy * 0.5 + 0.5;
        uvMin = min(uvMin, uv);
        uvMax = max(uvMax, uv);
        nearestDepth = min(nearestDepth, ndc.z);
    }

    // Off-screen parts have no occluder to read.
    uvMin = clamp(uvMin, vec2(0.0), vec2(1.0));
    uvMax = clamp(uvMax, vec2(0.0), vec2(1.0));

    // The mip where the rectangle spans at most one texel.
    vec2 rectPixels = (uvMax - uvMin) * screenSize;
    int mipLevel = int(ceil(log2(max(max(rectPixels.x, rectPixels.y), 1.0))));
    mipLevel = clamp(mipLevel, 0, int(hiZMipLevels) - 1);

    // Point-sampled, so the four corners hit every texel under the
    // rectangle; the pyramid stores the farthest depth, so take the max.
    float mip = float(mipLevel);
    float occluderDepth = max(
        max(textureLod(hiZBuffer, uvMin, mip).r,
            textureLod(hiZBuffer, vec2(uvMax.x, uvMin.y), mip).r),
        max(textureLod(hiZBuffer, vec2(uvMin.x, uvMax.y), mip).r,
            textureLod(hiZBuffer, uvMax, mip).r));

    // Occluded when even the nearest point lies behind the farthest occluder.
    return nearestDepth > occluderDepth;
}

#ifdef GPU_DRIVEN_CULL

layout(set = 0, binding = 0) uniform CullData
{
    mat4 view;
    mat4 proj;
    vec4 frustumPlanes[6];
    vec2 screenSize;
    uint drawCount;
    uint hiZMipLevels;
    uint enableOcclusionCulling;
    uint padding[2];
} cullData;

#endif // GPU_DRIVEN_CULL
#endif // GPU_DRIVEN_GLSL
