#include "stdafx.h"
#include "CameraUniformBuffer.h"

Core::CameraUniformBuffer::CameraUniformBuffer(VkDeviceSize bufferSize, uint32_t binding)
	:UniformBuffer(bufferSize, binding)
{
}

void Core::CameraUniformBuffer::MapBufferMemory(void* uniformBufferMapped)
{
	memcpy(uniformBufferMapped, &Matrices, sizeof(Matrices));
}
