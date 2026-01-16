#include "stdafx.h"
#include "DescriptorPool.h"
#include "IDescriptor.h"

using namespace Core;

Core::DescriptorSetLayout::DescriptorSetLayout(Device& device)
	: _device(device), _descriptorSetLayout(VK_NULL_HANDLE), _isFinalized(false)
{
}

Core::DescriptorSetLayout::~DescriptorSetLayout()
{
	if (_descriptorSetLayout != VK_NULL_HANDLE)
	{
		auto vkDevice = _device.GetDevice();
		vkDestroyDescriptorSetLayout(vkDevice, _descriptorSetLayout, nullptr);
	}
}

void Core::DescriptorSetLayout::AddUniformBufferBinding(uint32_t binding, VkShaderStageFlags stage, VkDeviceSize size)
{
	// Check if binding already exists and merge stages
	for (auto& existingBinding : _uniformBufferBindings)
	{
		if (existingBinding.Binding == binding)
		{
			existingBinding.Stage |= stage;
			return;
		}
	}
	
	// Add new binding
	_uniformBufferBindings.emplace_back(binding, stage, size);
}

void Core::DescriptorSetLayout::AddTextureBufferBinding(uint32_t binding, VkShaderStageFlags stage)
{
	// Check if binding already exists and merge stages
	for (auto& existingBinding : _textureBufferBindings)
	{
		if (existingBinding.Binding == binding)
		{
			existingBinding.Stage |= stage;
			return;
		}
	}
	
	// Add new binding
	_textureBufferBindings.emplace_back(binding, stage);
}

void Core::DescriptorSetLayout::AddStorageBufferBinding(uint32_t binding, VkShaderStageFlags stage)
{
	// Check if binding already exists and merge stages
	for (auto& existingBinding : _storageBufferBindings)
	{
		if (existingBinding.Binding == binding)
		{
			existingBinding.Stage |= stage;
			return;
		}
	}
	
	// Add new binding
	_storageBufferBindings.emplace_back(binding, stage);
}

void Core::DescriptorSetLayout::Finalize()
{
	if (_isFinalized)
		return;
	
	CreateDescriptorSetLayout();
	_isFinalized = true;
}

void Core::DescriptorSetLayout::CreateDescriptorSetLayout()
{
	vector<VkDescriptorSetLayoutBinding> bindings;
	
	// Collect all bindings
	for (auto& binding : _uniformBufferBindings)
	{
		bindings.push_back(binding.CreateDescriptorSetLayoutBinding());
	}
	
	for (auto& binding : _textureBufferBindings)
	{
		bindings.push_back(binding.CreateDescriptorSetLayoutBinding());
	}
	
	for (auto& binding : _storageBufferBindings)
	{
		bindings.push_back(binding.CreateDescriptorSetLayoutBinding());
	}

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
	layoutInfo.pBindings = bindings.data();

	if (vkCreateDescriptorSetLayout(
		_device.GetDevice(),
		&layoutInfo, nullptr, &_descriptorSetLayout) != VK_SUCCESS)
	{
		throw runtime_error("failed to create descriptor set layout!");
	}
}
