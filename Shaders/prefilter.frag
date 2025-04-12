#version 450

//Based on https://learnopengl.com/PBR/IBL/Specular-IBL

#define PI 3.1415926535897932384626433832795

#include "pbr.glsl"

layout(location = 0) in vec3 pos;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform samplerCube envmap;

layout(std140, push_constant) uniform PushConsts {
	layout(offset = 64) float roughness;
	layout(offset = 68) uint numSamples;
} consts;

void main()
{
	vec3 N = normalize(pos);
	vec3 R = N;
	vec3 V = R;

	float totalWeight = 0.0;
	vec3 prefilteredColor = vec3(0.0);

	float envMapDim = float(textureSize(envmap, 0).s);

	for (uint i = 0;i < consts.numSamples;++i)
	{
		vec2 Xi = Hammersley(i, consts.numSamples);
		vec3 H = ImportanceSampleGGX(Xi, N, consts.roughness);
		vec3 L = normalize(2.0 * dot(V, H) * H - V);

		float NdotL = max(dot(N, L), 0.0);
		if(NdotL > 0.0)
		{
			float D = DistributionGGX(N, H, consts.roughness);
			float pdf = D * NdotL / (4.0 * dot(V, H)) + 0.0001;

			// Solid angle of current smple
			float omegaS = 1.0 / (float(consts.numSamples) * pdf);
			// Solid angle of 1 pixel across all cube faces
			float omegaP = 4.0 * PI / (6.0 * envMapDim * envMapDim);

			// Biased (+1.0) mip level for better result
			float mipLevel = consts.roughness == 0.0 ? 0.0 : max(0.5 * log2(omegaS / omegaP) + 1.0, 0.0f);

			prefilteredColor += textureLod(envmap, L, mipLevel).rgb * NdotL;
			totalWeight += NdotL;
		}
	}

	prefilteredColor /= totalWeight;
	outColor = vec4(prefilteredColor, 1.0);
}