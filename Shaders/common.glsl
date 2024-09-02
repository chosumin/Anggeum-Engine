layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;
layout(binding = 1) uniform sampler2D texSampler;
layout(binding = 2) uniform sampler2D shadowmap;
layout(binding = 3) uniform ShadowUniform
{
	mat4 light;
} shadow;