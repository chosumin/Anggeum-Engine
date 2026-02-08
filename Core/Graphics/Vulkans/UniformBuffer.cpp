#include "stdafx.h"
#include "UniformBuffer.h"
#include "Buffer.h"
#include "MemoryAllocator.h"

Core::UniformBuffer::UniformBuffer(Device& device, VkDeviceSize bufferSize)
	:_device(device)
{
	CreateUniformBuffer(bufferSize);

	_bufferInfo.range = bufferSize;
}

Core::UniformBuffer::~UniformBuffer()
{
}

void Core::UniformBuffer::Update(void* data)
{
	memcpy(_uniformBufferMapped, data, _buffer->GetSize());
}

void Core::UniformBuffer::Update(void* data, VkDeviceSize offset, VkDeviceSize size)
{
	memcpy(static_cast<char*>(_uniformBufferMapped) + offset, data, size);
}

VkWriteDescriptorSet Core::UniformBuffer::CreateWriteDescriptorSet(uint32_t binding)
{
	_bufferInfo.buffer = _buffer->GetBuffer();
	_bufferInfo.offset = 0;
	_bufferInfo.range = _buffer->GetSize();

	VkWriteDescriptorSet descriptorWrite{};
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstBinding = binding;
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pBufferInfo = &_bufferInfo;

	return descriptorWrite;
}

void Core::UniformBuffer::CreateUniformBuffer(VkDeviceSize bufferSize)
{
	_buffer = make_unique<Buffer>(_device,
		bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		MemoryType::UNIFORM);

	_buffer->GetMappedPtr(&_uniformBufferMapped);
}

Core::UniformBufferLayoutBinding::UniformBufferLayoutBinding(uint32_t binding, VkShaderStageFlags stage, VkDeviceSize bufferSize)
	:Binding(binding), Stage(stage), BufferSize(bufferSize)
{
}