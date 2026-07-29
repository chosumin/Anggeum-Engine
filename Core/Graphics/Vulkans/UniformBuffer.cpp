#include "stdafx.h"
#include "UniformBuffer.h"
#include "Buffer.h"

void Core::UniformBuffer::SetBuffer(Buffer* data)
{
	_buffer = data;
	_bufferInfo.range = data->GetSize();
}

VkWriteDescriptorSet Core::UniformBuffer::CreateWriteDescriptorSet(uint32_t binding)
{
	_bufferInfo.buffer = _buffer->GetBuffer();
	_bufferInfo.offset = 0;

	VkWriteDescriptorSet descriptorWrite{};
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstBinding = binding;
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pBufferInfo = &_bufferInfo;

	return descriptorWrite;
}

Core::UniformBufferLayoutBinding::UniformBufferLayoutBinding(uint32_t binding, VkShaderStageFlags stage, VkDeviceSize bufferSize)
	:Binding(binding), Stage(stage), BufferSize(bufferSize)
{
}
