#version 450

#include "common.glsl"

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform instanceData
{
    mat4 world;
} instance;

void main() 
{
    gl_Position = camera.proj * camera.view * instance.world * vec4(inPosition, 1.0);
}