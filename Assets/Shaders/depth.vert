#version 450

#include "common.glsl"
#include "gpuDriven.glsl"

layout(location = 0) in vec3 inPosition;

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
    uint instanceIndex = instanceIDs.IDs[gl_InstanceIndex];
    mat4 world = transformBuffer.transforms[instanceData.instances[instanceIndex].transformIndex];

    gl_Position = camera.proj * camera.view * world * vec4(inPosition, 1.0);
}