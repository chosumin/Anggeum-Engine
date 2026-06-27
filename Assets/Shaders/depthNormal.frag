#version 450

layout(location = 0) in  vec3 inWorldNormal;
// layout(location = 0) ? color attachment [0] (RT_MAIN_NORMAL)
layout(location = 0) out vec4 outNormal;

void main()
{
    vec3 N = normalize(inWorldNormal);
    vec3 packedNormal = N * 0.5 + 0.5;

    outNormal = vec4(packedNormal, 1.0);
}