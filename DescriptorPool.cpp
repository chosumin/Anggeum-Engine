#include "stdafx.h"
#include "DescriptorPool.h"
#include "IDescriptor.h"
using namespace Core;

Core::DescriptorPool::DescriptorPool(Device& device, vector<IDescriptor*>& descriptors)
	:_device(device), _poolCreateInfo()
{
	CreatePoolCreateInfo(descriptors);
	CreateDescriptorSetLayout(descriptors);
}

Core::DescriptorPool::~DescriptorPool()
{
	auto vkDevice = _device.GetDevice();

	for (auto& descriptorPool : _descriptorPools)
		vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);

	_descriptorPools.clear();

	vkDestroyDescriptorSetLayout(vkDevice, _descriptorSetLayout, nullptr);
}

VkDescriptorPool& Core::DescriptorPool::AllocateDescriptorPool()
{
	VkDescriptorPool descriptorPool;

	if (vkCreateDescriptorPool(_device.GetDevice(),
		&_poolCreateInfo, nullptr, &descriptorPool) != VK_SUCCESS)
	{
		throw runtime_error("failed to create descriptor pool!");
	}

	_descriptorPools.push_back(descriptorPool);

	return *(_descriptorPools.end() - 1);
}

void Core::DescriptorPool::CreatePoolCreateInfo(vector<IDescriptor*> descriptors)
{
	size_t size = descriptors.size();

	_poolSize.resize(size);

	for (size_t i = 0; i < size; i++)
	{
		_poolSize[i].type = descriptors[i]->GetDescriptorType();
		_poolSize[i].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	}

	_poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	_poolCreateInfo.poolSizeCount = static_cast<uint32_t>(_poolSize.size());
	_poolCreateInfo.pPoolSizes = _poolSize.data();
	_poolCreateInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
}

void Core::DescriptorPool::CreateDescriptorSetLayout(vector<IDescriptor*> descriptors)
{
	size_t size = descriptors.size();

	vector<VkDescriptorSetLayoutBinding> bindings(size);
	for (size_t i = 0; i < size; i++)
	{
		bindings[i] = (descriptors[i]->CreateDescriptorSetLayoutBinding());
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
