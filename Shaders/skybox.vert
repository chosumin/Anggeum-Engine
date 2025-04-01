#version 450

#include "common.glsl"

layout(location = 0) in vec3 position;

layout(location = 0) out vec3 outPos;

void main() 
{   
    outPos = position;

    mat4 rotView = mat4(mat3(camera.view));
    vec4 pos = camera.proj * rotView * vec4(position, 1.0);
    pos.z = pos.w - 0.00001;

    gl_Position = pos;
}