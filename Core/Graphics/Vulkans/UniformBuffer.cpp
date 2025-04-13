#include "stdafx.h"
#include "UniformBuffer.h"
#include "Buffer.h"

Core::UniformBuffer::UniformBuffer(Device& device, VkDeviceSize bufferSize)
	:_device(device)
{
	CreateUniformBuffer(bufferSize);

	_bufferInfo.range = bufferSize;
}

Core::UniformBuffer::~UniformBuffer()
{
}

void Core::UniformBuffer::SetBuffer(uint32_t currentImage, void* data)
{
	if (_uniformBuffersMapped.size() > 0 &&
		_uniformBuffersMapped[currentImage] != nullptr)
		memcpy(_uniformBuffersMapped[currentImage], data, _bufferInfo.range);
}

VkWriteDescriptorSet Core::UniformBuffer::CreateWriteDescriptorSet(size_t index, uint32_t binding)
{
	_bufferInfo.buffer = _buffers[index]->GetBuffer();
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

void Core::UniformBuffer::CreateUniformBuffer(VkDeviceSize bufferSize)
{
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

Core::UniformBufferLayoutBinding::UniformBufferLayoutBinding(uint32_t binding, VkShaderStageFlagBits stage, VkDeviceSize bufferSize)
	:Binding(binding), Stage(stage), BufferSize(bufferSize)
{
}

VkDescriptorSetLayoutBinding Core::UniformBufferLayoutBinding::CreateDescriptorSetLayoutBinding()
{
	VkDescriptorSetLayoutBinding uboLayoutBinding{};
	uboLayoutBinding.binding = Binding;
	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboLayoutBinding.descriptorCount = 1;
	uboLayoutBinding.stageFlags = Stage;
	uboLayoutBinding.pImmutableSamplers = nullptr;

	return uboLayoutBinding;
}

VkDescriptorType Core::UniformBufferLayoutBinding::GetDescriptorType()
{
	return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}
