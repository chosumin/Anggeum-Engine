#version 450

#include "common.glsl"

layout(location = 0) in vec3 inPosition;

layout(set = 1, binding = 1) buffer readonly TransformBuffer
{
    mat4 transforms[];
} transformBuffer;

layout(set = 1, binding = 2) buffer readonly InstanceBuffer
{
    uint IDs[];
} instanceBuffer;

void main() 
{
    uint id = instanceBuffer.IDs[gl_InstanceIndex];
    mat4 world = transformBuffer.transforms[id];

    gl_Position = camera.proj * camera.view * world * vec4(inPosition, 1.0);
}