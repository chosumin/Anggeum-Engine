#version 450

//Based on https://learnopengl.com/PBR/IBL/Specular-IBL and https://github.com/SaschaWillems/Vulkan-glTF-PBR/blob/master/data/shaders/genbrdflut.frag


#define PI 3.1415926535897932384626433832795

#include "pbr.glsl"

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outColor;

layout(constant_id = 0) const uint NUM_SAMPLES = 1024u;

//Store roughness / NdotV as a look up table
vec2 IntergrateBRDF(float NdotV, float roughness)
{
	vec3 V;
	V.x = sqrt(1.0 - NdotV * NdotV);
	V.y = 0;
	V.z = NdotV;

	float A = 0;
	float B = 0;

	vec3 N = vec3(0.0, 0.0, 1.0);

	for(uint i = 0; i < NUM_SAMPLES; i++)
	{
		//sample the hemisphere using Hammersley sequence
		vec2 Xi = Hammersley(i, NUM_SAMPLES);
		vec3 H = ImportanceSampleGGX(Xi, N, roughness);
		vec3 L = normalize(2.0 * dot(V, H) * H - V);
		
		float NdotL = max(dot(N, L), 0.0);
		float NdotH = max(dot(N, H), 0.0);
		float VdotH = max(dot(V, H), 0.0);
		float NdotV = max(dot(N, V), 0.0);

		if (NdotL > 0.0)
		{
			float G = GeometrySmithForIBL(N, V, L, roughness);
			float G_Vis = (G * VdotH) / (NdotH * NdotV);
			float Fc = pow(1.0 - VdotH, 5.0);

			A += (1.0 - Fc) * G_Vis;
			B += Fc * G_Vis;
		}
	}

	A /= float(NUM_SAMPLES);
	B /= float(NUM_SAMPLES);
	return vec2(A, B);

}

//irradiance cubemap using convolution
void main()
{
	vec2 integratedBRDF = IntergrateBRDF(inUV.x, inUV.y);
	outColor = vec4(integratedBRDF, 0.0, 1.0);
}