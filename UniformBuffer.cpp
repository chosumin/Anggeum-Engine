#include "stdafx.h"
#include "UniformBuffer.h"
#include "SwapChain.h"
#include "Buffer.h"

Core::UniformBuffer::UniformBuffer(VkDeviceSize bufferSize)
	:_bufferSize(bufferSize)
{
	CreateDescriptorSetLayout();
	CreateUniformBuffer();
	CreateDescriptorPool();
	CreateDescriptorSets();
}

Core::UniformBuffer::~UniformBuffer()
{
	auto device = Device::GetInstance().GetDevice();

	vkDestroyDescriptorPool(device, _descriptorPool, nullptr);

	vkDestroyDescriptorSetLayout(device, _descriptorSetLayout, nullptr);
}

void Core::UniformBuffer::Update(VkCommandBuffer commandBuffer,
	uint32_t currentImage, VkPipelineLayout pipelineLayout)
{
	MapBufferMemory(_uniformBuffersMapped[currentImage]);

	vkCmdBindDescriptorSets(
		commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipelineLayout, 0, 1,
		&_descriptorSets[currentImage], 0, nullptr);
}

void Core::UniformBuffer::CreateDescriptorSetLayout()
{
	VkDescriptorSetLayoutBinding uboLayoutBinding{};
	uboLayoutBinding.binding = 0;
	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboLayoutBinding.descriptorCount = 1;
	uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	uboLayoutBinding.pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = &uboLayoutBinding;

	if (vkCreateDescriptorSetLayout(
		Device::GetInstance().GetDevice(),
		&layoutInfo, nullptr, &_descriptorSetLayout) != VK_SUCCESS)
	{
		throw runtime_error("failed to create descriptor set layout!");
	}
}

void Core::UniformBuffer::CreateUniformBuffer()
{
	VkDeviceSize bufferSize = _bufferSize;

	_buffers.resize(MAX_FRAMES_IN_FLIGHT);
	_uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		_buffers[i] = make_unique<Buffer>(
			bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		//persistent mapping
		//The uniform data will be used for all draw calls, 
		//so the buffer containing it should only be destroyed when we stop rendering.
		auto bufferMemory = _buffers[i]->GetBufferMemory();
		vkMapMemory(
			Device::GetInstance().GetDevice(), bufferMemory, 
			0, bufferSize, 0, &_uniformBuffersMapped[i]);
	}
}

void Core::UniformBuffer::CreateDescriptorPool()
{
	VkDescriptorPoolSize poolSize{};
	poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSize.descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	poolInfo.flags = 0;

	if (vkCreateDescriptorPool(Device::GetInstance().GetDevice(), 
		&poolInfo, nullptr, &_descriptorPool) != VK_SUCCESS)
	{
		throw runtime_error("failed to create descriptor pool!");
	}
}

void Core::UniformBuffer::CreateDescriptorSets()
{
	vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, _descriptorSetLayout);
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = _descriptorPool;
	allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	allocInfo.pSetLayouts = layouts.data();

	_descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);

	if (vkAllocateDescriptorSets(Device::GetInstance().GetDevice(), 
		&allocInfo, _descriptorSets.data()) != VK_SUCCESS) 
	{
		throw runtime_error("failed to allocate descriptor sets!");
	}

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) 
	{
		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = _buffers[i]->GetBuffer();
		bufferInfo.offset = 0;
		bufferInfo.range = _bufferSize;

		VkWriteDescriptorSet descriptorWrite{};
		descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrite.dstSet = _descriptorSets[i];
		descriptorWrite.dstBinding = 0;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.pBufferInfo = &bufferInfo;
		descriptorWrite.pImageInfo = nullptr; // Optional
		descriptorWrite.pTexelBufferView = nullptr; // Optional

		vkUpdateDescriptorSets(Device::GetInstance().GetDevice(), 1, &descriptorWrite, 0, nullptr);
	}
}
