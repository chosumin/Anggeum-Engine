#version 450

layout(location = 0) in vec3 position;

layout(location = 0) out vec3 outPos;

layout(push_constant) uniform MVP {
	mat4 mvp;
} mvp;

void main() 
{   
    outPos = position;

    gl_Position = mvp.mvp * vec4(position, 1.0);
}