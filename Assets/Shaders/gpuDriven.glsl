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
    vec4 boundingSphere;  // xyz: center (local), w: radius
    uint transformIndex;
    uint drawCommandIndex;
    uint materialIndex;
    uint padding;
};

// The instance's bounding sphere under its world transform.
void InstanceWorldSphere(InstanceData inst, mat4 world, out vec3 center, out float radius)
{
    center = (world * vec4(inst.boundingSphere.xyz, 1.0)).xyz;

    float maxScale = max(length(world[0].xyz),
        max(length(world[1].xyz), length(world[2].xyz)));
    radius = inst.boundingSphere.w * maxScale;
}

bool IsVisibleFrustum(vec4 frustumPlanes[6], vec3 center, float radius)
{
    for (int i = 0; i < 6; ++i)
    {
        if (dot(frustumPlanes[i].xyz, center) + frustumPlanes[i].w < -radius)
            return false;
    }
    return true;
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

// Hi-Z test against the sphere's nearest point; false when the sphere
// straddles the near plane or leaves the screen (frustum culling owns those).
bool IsOccluded(sampler2D hiZBuffer, vec3 worldCenter, float worldRadius)
{
    vec4 viewCenter = cullData.view * vec4(worldCenter, 1.0);
    vec4 clipCenter = cullData.proj * viewCenter;
    if (clipCenter.w <= 0.0)
        return false;

    vec2 screenUV = (clipCenter.xy / clipCenter.w) * 0.5 + 0.5;
    if (any(lessThan(screenUV, vec2(0.0))) || any(greaterThan(screenUV, vec2(1.0))))
        return false;

    // abs(): Vulkan's flipped Y makes proj[1][1] negative.
    float projRadius = worldRadius * abs(cullData.proj[1][1]) / clipCenter.w;
    float screenPixelRadius = projRadius * cullData.screenSize.y * 0.5;
    int mipLevel = int(ceil(log2(max(screenPixelRadius * 2.0, 1.0))));
    mipLevel = clamp(mipLevel, 0, int(cullData.hiZMipLevels) - 1);
    float occluderDepth = textureLod(hiZBuffer, screenUV, float(mipLevel)).r;

    // View space looks down -Z: the nearest point is the closest z.
    float viewNearZ = viewCenter.z + worldRadius;
    if (viewNearZ >= 0.0)
        return false;

    vec4 clipNear = cullData.proj * vec4(0.0, 0.0, viewNearZ, 1.0);
    float objectNearDepth = clipNear.z / clipNear.w;
    return objectNearDepth > occluderDepth;
}

#endif // GPU_DRIVEN_CULL
#endif // GPU_DRIVEN_GLSL
