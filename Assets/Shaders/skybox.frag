#version 450

#include "common.glsl"

layout(location = 0) in vec3 pos;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 1) uniform samplerCube envmap;

void main()
{
	// The cubemap holds linear HDR radiance; no sRGB decode.
	vec3 envColor = textureLod(envmap, pos, 1.2).rgb;

	outColor = Tonemap(vec4(envColor, 1.0), FORWARD_EXPOSURE, 1.0);
}
