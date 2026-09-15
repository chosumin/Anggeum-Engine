#version 450

#include "common.glsl"
#include "gpuDriven.glsl"

// Shared by the depth prepass (with depthNormal.frag) and the geometry pass;
// invariance guarantees both pipelines emit bit-identical positions, so the
// geometry pass survives LESS_OR_EQUAL on the prepass depth with writes off.
invariant gl_Position;

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;

layout(location = 0) out vec4 worldPos;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 uv;
layout(location = 3) flat out uint outMaterialIndex;

layout(set = 0, binding = 1) buffer readonly TransformBuffer
{
    mat4 transforms[];
} transformBuffer;

layout(set = 0, binding = 2) buffer readonly InstanceIDBuffer
{
    uint IDs[];
} instanceIDs;

layout(set = 0, binding = 9) buffer readonly InstanceDataBuffer
{
    InstanceData instances[];
} instanceData;

void main()
{
    InstanceData inst = instanceData.instances[instanceIDs.IDs[gl_InstanceIndex]];
    mat4 world = transformBuffer.transforms[inst.transformIndex];
    outMaterialIndex = inst.materialIndex;

    worldPos = world * vec4(position, 1.0);

    worldNormal = normalize(mat3(transpose(inverse(world))) * normal);

    uv = texCoord;
    
    gl_Position = camera.proj * camera.view * worldPos;
}