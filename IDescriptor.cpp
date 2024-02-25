#include "stdafx.h"
#include "IDescriptor.h"

Core::Descriptor::Descriptor(uint32 binding)
	:Binding(binding)
{
	DescriptorSetLayoutBinding.binding = binding;
	WriteDescriptorSet.dstBinding = binding;
}

VkDescriptorSetLayoutBinding Core::Descriptor::CreateDescriptorSetLayoutBinding()
{
	VkDescriptorSetLayoutBinding uboLayoutBinding{};
	uboLayoutBinding.binding = Binding;
	//uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboLayoutBinding.descriptorCount = 1;
	uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	uboLayoutBinding.pImmutableSamplers = nullptr;

	return uboLayoutBinding;
}

VkWriteDescriptorSet Core::Descriptor::CreateWriteDescriptorSet(size_t index)
{
	_bufferInfo.buffer = _buffers[index]->GetBuffer();
	_bufferInfo.offset = 0;
	_bufferInfo.range = _bufferSize;

	VkWriteDescriptorSet descriptorWrite{};
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	//descriptorWrite.dstBinding = Binding;
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pBufferInfo = &_bufferInfo;

	return descriptorWrite;
}

void Core::Descriptor::SetDescriptorType(VkDescriptorType type)
{
	DescriptorSetLayoutBinding.descriptorType = type;
	DescriptorType = type;
}