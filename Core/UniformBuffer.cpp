#include "stdafx.h"
#include "UniformBuffer.h"
#include "Buffer.h"

Core::UniformBuffer::UniformBuffer(Device& device, 
	VkDeviceSize bufferSize, uint32_t binding)
	:_bufferSize(bufferSize), _binding(binding), _device(device)
{
	CreateUniformBuffer();
}

Core::UniformBuffer::~UniformBuffer()
{
}

void Core::UniformBuffer::SetBuffer(uint32_t currentImage, void* data)
{
	memcpy(_uniformBuffersMapped[currentImage], data, _bufferSize);
}

VkWriteDescriptorSet Core::UniformBuffer::CreateWriteDescriptorSet(size_t index)
{
	_bufferInfo.buffer = _buffers[index]->GetBuffer();
	_bufferInfo.offset = 0;
	_bufferInfo.range = _bufferSize;

	VkWriteDescriptorSet descriptorWrite{};
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstBinding = _binding;
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pBufferInfo = &_bufferInfo;

	return descriptorWrite;
}

void Core::UniformBuffer::CreateUniformBuffer()
{
	VkDeviceSize bufferSize = _bufferSize;

	_buffers.resize(MAX_FRAMES_IN_FLIGHT);
	_uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		_buffers[i] = make_unique<Buffer>(_device,
			bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		//persistent mapping
		//The uniform data will be used for all draw calls, 
		//so the buffer containing it should only be destroyed when we stop rendering.
		auto bufferMemory = _buffers[i]->GetBufferMemory();
		vkMapMemory(
			_device.GetDevice(), bufferMemory,
			0, bufferSize, 0, &_uniformBuffersMapped[i]);
	}
}

Core::UniformBufferLayoutBinding::UniformBufferLayoutBinding(uint32_t binding)
	:_binding(binding)
{
}

VkDescriptorSetLayoutBinding Core::UniformBufferLayoutBinding::CreateDescriptorSetLayoutBinding()
{
	VkDescriptorSetLayoutBinding uboLayoutBinding{};
	uboLayoutBinding.binding = _binding;
	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboLayoutBinding.descriptorCount = 1;
	uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	uboLayoutBinding.pImmutableSamplers = nullptr;

	return uboLayoutBinding;
}

VkDescriptorType Core::UniformBufferLayoutBinding::GetDescriptorType()
{
	return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}