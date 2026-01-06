#version 450

#include "common.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;

layout(push_constant) uniform instanceData
{
    mat4 world;
} instance;

layout(location = 0) out vec4 worldPos;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 uv;

void main() 
{
    worldPos = instance.world * vec4(position, 1.0);

    worldNormal = normalize(mat3(transpose(inverse(instance.world))) * normal);

    uv = texCoord;
    
    gl_Position = camera.proj * camera.view * worldPos;
}