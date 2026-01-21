#include "stdafx.h"
#include "StorageBuffer.h"
#include "Buffer.h"
#include "MemoryAllocator.h"

Core::StorageBuffer::StorageBuffer()
	: _buffer(nullptr)
{
}

Core::StorageBuffer::~StorageBuffer()
{
}

void Core::StorageBuffer::SetBuffer(Buffer* data)
{
	_buffer = data;
	_bufferInfo.range = data->GetSize();
}

VkWriteDescriptorSet Core::StorageBuffer::CreateWriteDescriptorSet(uint32_t binding)
{
	_bufferInfo.buffer = _buffer->GetBuffer();
	_bufferInfo.offset = 0;

	VkWriteDescriptorSet descriptorWrite{};
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstBinding = binding;
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pBufferInfo = &_bufferInfo;

	return descriptorWrite;
}

Core::StorageBufferLayoutBinding::StorageBufferLayoutBinding(uint32_t binding, VkShaderStageFlags stage)
	:Binding(binding), Stage(stage)
{
}

VkDescriptorSetLayoutBinding Core::StorageBufferLayoutBinding::CreateDescriptorSetLayoutBinding()
{
	VkDescriptorSetLayoutBinding layoutBinding{};
	layoutBinding.binding = Binding;
	layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	layoutBinding.descriptorCount = 1;
	layoutBinding.stageFlags = Stage;
	layoutBinding.pImmutableSamplers = nullptr;

	return layoutBinding;
}

VkDescriptorType Core::StorageBufferLayoutBinding::GetDescriptorType()
{
	return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
}
