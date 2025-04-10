#version 450

layout(location = 0) in vec3 pos;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform samplerCube envmap;

layout(std140, push_constant) uniform PushConsts {
	layout(offset = 64) float deltaPhi;
	layout(offset = 68) float deltaTheta;
} consts;

#define PI 3.1415926535897932384626433832795

//irradiance cubemap using convolution
void main()
{
	vec3 N = normalize(pos);
	
	vec3 up = vec3(0.0, 1.0, 0.0);
	vec3 right = normalize(cross(up, N));
	up = normalize(cross(N, right));

	const float TWO_PI = PI * 2.0;
	const float HALF_PI = PI * 0.5;

	vec3 irradiance = vec3(0.0);
	uint sampleCount = 0;

	//convolution arround the ring of the hemisphere
	for(float phi = 0.0;phi < TWO_PI; phi += consts.deltaPhi)
	{
		//the inclination zenith to sample the ring of the hemisphere
		for(float theta = 0.0; theta < HALF_PI; theta += consts.deltaTheta)
		{
			//tangent space
			vec3 tempVec = cos(phi) * right + sin(phi) * up;
			
			//a direction in the hemisphere around the N
			vec3 sampleVector = cos(theta) * N + sin(theta) * tempVec;
			
			irradiance += texture(envmap, sampleVector).rgb * sin(theta) * cos(theta);
			sampleCount++;
		}
	}

	outColor = vec4(PI * irradiance / float(sampleCount), 1.0);
}