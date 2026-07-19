#include "stdafx.h"
#include "UniformBuffer.h"

Core::UniformBufferLayoutBinding::UniformBufferLayoutBinding(uint32_t binding, VkShaderStageFlags stage, VkDeviceSize bufferSize)
	:Binding(binding), Stage(stage), BufferSize(bufferSize)
{
}
