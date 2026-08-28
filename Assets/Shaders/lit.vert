#version 450
#extension GL_ARB_shader_draw_parameters : require

#include "common.glsl"

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
layout(location = 3) flat out uint outDrawID; 

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
    outDrawID = gl_InstanceIndex;

    uint id = instanceBuffer.IDs[gl_InstanceIndex];
    mat4 world = transformBuffer.transforms[id];

    worldPos = world * vec4(position, 1.0);

    worldNormal = normalize(mat3(transpose(inverse(world))) * normal);

    uv = texCoord;
    
    gl_Position = camera.proj * camera.view * worldPos;
}