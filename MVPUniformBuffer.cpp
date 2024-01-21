#include "stdafx.h"
#include "MVPUniformBuffer.h"

Core::ModelUniformBuffer::ModelUniformBuffer()
	:UniformBuffer(sizeof(mat4), 1)
{
	BufferObject = mat4(1);
}

void Core::ModelUniformBuffer::MapBufferMemory(void* uniformBufferMapped)
{
	memcpy(uniformBufferMapped, &BufferObject, sizeof(mat4));
}
