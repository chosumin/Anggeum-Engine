#version 450
#extension GL_ARB_shader_draw_parameters : require

#include "common.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec4 outWorldPos;
layout(location = 1) out vec3 outWorldNormal;
layout(location = 2) out vec2 outUV;
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

    uint id    = instanceBuffer.IDs[gl_InstanceIndex];
    mat4 world = transformBuffer.transforms[id];

    outWorldPos = world * vec4(inPosition, 1.0);

    mat3 normalMatrix = transpose(inverse(mat3(world)));
    outWorldNormal = normalize(normalMatrix * inNormal);

    outUV = inTexCoord;

    gl_Position = camera.proj * camera.view * outWorldPos;
}