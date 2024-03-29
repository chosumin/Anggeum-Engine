#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec3 inPosition;
layout (location = 1) in mat4 instanceMat;

void main() {
    gl_Position = ubo.proj * ubo.view * instanceMat * vec4(inPosition, 1.0);
}