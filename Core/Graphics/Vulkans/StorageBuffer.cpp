#include "stdafx.h"
#include "StorageBuffer.h"
#include "Buffer.h"
#include "MemoryAllocator.h"

Core::StorageBuffer::StorageBuffer()
{
	_buffers.resize(MAX_FRAMES_IN_FLIGHT);
}

Core::StorageBuffer::~StorageBuffer()
{
}

void Core::StorageBuffer::SetBuffer(uint32_t currentImage, Buffer* data, u32 arrayLength)
{
	_buffers[currentImage] = data;
	_bufferInfo.range = arrayLength;
}

VkWriteDescriptorSet Core::StorageBuffer::CreateWriteDescriptorSet(size_t index, uint32_t binding)
{
	_bufferInfo.buffer = _buffers[index]->GetBuffer();
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
