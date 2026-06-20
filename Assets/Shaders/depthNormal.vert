#version 450

#include "common.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 outWorldNormal;

layout(set = 0, binding = 1) buffer readonly TransformBuffer
{
    mat4 transforms[];
} transformBuffer;

layout(set = 0, binding = 2) buffer readonly InstanceBuffer
{
    uint IDs[];
} instanceBuffer;

void main()
{
    uint id    = instanceBuffer.IDs[gl_InstanceIndex];
    mat4 world = transformBuffer.transforms[id];

    mat3 normalMatrix = transpose(inverse(mat3(world)));
    outWorldNormal    = normalize(normalMatrix * inNormal);

    gl_Position = camera.proj * camera.view * world * vec4(inPosition, 1.0);
}