#version 450

#include "common.glsl"

layout(location = 0) in vec3 pos;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 1) uniform samplerCube envmap;

void main()
{
	vec3 envColor = SRGBtoLINEAR(textureLod(envmap, pos, 1.2)).rgb;

	envColor = envColor / (envColor + vec3(1.0));

    outColor = vec4(envColor, 1.0);
}