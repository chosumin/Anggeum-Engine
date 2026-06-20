#version 450

layout(location = 0) in  vec3 inWorldNormal;
// layout(location = 0) ? color attachment [0] (RT_MAIN_NORMAL)
layout(location = 0) out vec4 outNormal;

void main()
{
    outNormal = vec4(normalize(inWorldNormal), 0.0);
}