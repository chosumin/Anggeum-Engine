#include "stdafx.h"
#include "MVPUniformBuffer.h"

Core::MVPUniformBuffer::MVPUniformBuffer(float width, float height)
	:UniformBuffer(sizeof(MVPBufferObject))

{
	_bufferObject.Model = mat4(1);

	_bufferObject.View = lookAt(
		vec3(2.0f, 2.0f, 2.0f),
		vec3(0.0f, 0.0f, 0.0f),
		vec3(0.0f, 0.0f, 1.0f));

	_bufferObject.Proj = perspective(
		radians(45.0f),
		width / height,
		0.1f, 10.0f);
	_bufferObject.Proj[1][1] *= -1;
}

void Core::MVPUniformBuffer::MapBufferMemory(void* uniformBufferMapped)
{
	static auto startTime = chrono::high_resolution_clock::now();

	auto currentTime = chrono::high_resolution_clock::now();
	float time = chrono::duration<float, chrono::seconds::period>(currentTime - startTime).count();

	_bufferObject.Model = glm::rotate(
		glm::mat4(1.0f),
		time * glm::radians(90.0f),
		glm::vec3(0.0f, 0.0f, 1.0f));

	//copy the buffer to the buffer memory
	memcpy(uniformBufferMapped, &_bufferObject, sizeof(MVPBufferObject));
}