#version 450

#include "common.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(push_constant) uniform instanceData
{
    mat4 world;
} instance;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec4 worldPos;

void main() 
{
    worldPos = instance.world * vec4(inPosition, 1.0);
    gl_Position = camera.proj * camera.view * worldPos;
    fragColor = inColor;
    fragTexCoord = inTexCoord;
}