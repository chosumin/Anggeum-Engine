#version 450

#include "common.glsl"

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec4 worldPos;

layout(location = 0) out vec4 outColor;

float CalculateShadow()
{
	vec4 projectedCoord = shadow.light * worldPos;

    projectedCoord /= projectedCoord.w;

    projectedCoord.xy = 0.5 * projectedCoord.xy + 0.5;

	float lightDepth = texture(shadowmap, projectedCoord.xy).r;

	return lightDepth.r > projectedCoord.z  - 0.00005f ? 1.0f : 0.0f;
}

void main()
{
	float light = 1.0f * CalculateShadow();
	
	vec4 base = texture(texSampler, fragTexCoord);
	outColor = vec4(base.rgb * light, base.a);
}