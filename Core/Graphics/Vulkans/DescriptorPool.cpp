#include "stdafx.h"
#include "DescriptorPool.h"

namespace Core
{
	// ============================================
	// Descriptor Set Layout Implementation
	// ============================================
	DescriptorSetLayout::DescriptorSetLayout(Device& device)
		: _device(device), _type(DescriptorSetType::Material)
	{
	}

	DescriptorSetLayout::DescriptorSetLayout(Device& device, DescriptorSetType type)
		: _device(device), _type(type)
	{
	}

	DescriptorSetLayout::~DescriptorSetLayout()
	{
		if (_descriptorSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(_device.GetDevice(), _descriptorSetLayout, nullptr);
			_descriptorSetLayout = VK_NULL_HANDLE;
		}
	}

	void DescriptorSetLayout::AddUniformBufferBinding(
		uint32_t binding,
		VkShaderStageFlags stage,
		VkDeviceSize size)
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

	void DescriptorSetLayout::AddTextureBufferBinding(uint32_t binding, VkShaderStageFlags stage, 
		VkDescriptorType descriptorType)
	{
		_textureBufferBindings.emplace_back(binding, stage, descriptorType);
	}

	void DescriptorSetLayout::AddStorageBufferBinding(
		uint32_t binding,
		VkShaderStageFlags stage)
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

	void DescriptorSetLayout::Finalize()
	{
		if (_isFinalized)
			return;

		CreateDescriptorSetLayout();
		_isFinalized = true;
	}

	void DescriptorSetLayout::CreateDescriptorSetLayout()
	{
		vector<VkDescriptorSetLayoutBinding> bindings;

		// Uniform buffer bindings
		for (const auto& binding : _uniformBufferBindings)
		{
			VkDescriptorSetLayoutBinding layoutBinding{};
			layoutBinding.binding = binding.Binding;
			layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			layoutBinding.descriptorCount = 1;
			layoutBinding.stageFlags = binding.Stage;
			layoutBinding.pImmutableSamplers = nullptr;

			bindings.push_back(layoutBinding);
		}

		// Texture buffer bindings
		for (const auto& texBinding : _textureBufferBindings)
		{
			VkDescriptorSetLayoutBinding layoutBinding{};
			layoutBinding.binding = texBinding.Binding;
			layoutBinding.descriptorType = texBinding.DescriptorType;
			layoutBinding.descriptorCount = 1;
			layoutBinding.stageFlags = texBinding.Stage;
			layoutBinding.pImmutableSamplers = nullptr;
			bindings.push_back(layoutBinding);
		}

		// Storage buffer bindings
		for (const auto& binding : _storageBufferBindings)
		{
			VkDescriptorSetLayoutBinding layoutBinding{};
			layoutBinding.binding = binding.Binding;
			layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			layoutBinding.descriptorCount = 1;
			layoutBinding.stageFlags = binding.Stage;
			layoutBinding.pImmutableSamplers = nullptr;

			bindings.push_back(layoutBinding);
		}

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();

		if (vkCreateDescriptorSetLayout(_device.GetDevice(), &layoutInfo, nullptr, &_descriptorSetLayout) != VK_SUCCESS)
		{
			throw runtime_error("Failed to create descriptor set layout!");
		}
	}

	// ============================================
	// Descriptor Pool Implementation
	// ============================================
	DescriptorPool::DescriptorPool(Device& device)
		: _device(device)
	{
	}

	DescriptorPool::~DescriptorPool()
	{
		if (_descriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(_device.GetDevice(), _descriptorPool, nullptr);
			_descriptorPool = VK_NULL_HANDLE;
		}
	}

	void DescriptorPool::CreatePool(
		const vector<VkDescriptorPoolSize>& poolSizes,
		uint32_t maxSets)
	{
		_maxSets = maxSets;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();
		poolInfo.maxSets = maxSets;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // 개별 해제 가능

		if (vkCreateDescriptorPool(_device.GetDevice(), &poolInfo, nullptr, &_descriptorPool) != VK_SUCCESS)
		{
			throw runtime_error("Failed to create descriptor pool!");
		}
	}

	void DescriptorPool::Reset()
	{
		if (_descriptorPool == VK_NULL_HANDLE)
			return;

		// 모든 descriptor sets를 한 번에 해제 (매우 효율적)
		vkResetDescriptorPool(_device.GetDevice(), _descriptorPool, 0);
	}

	VkDescriptorSet DescriptorPool::AllocateDescriptorSet(VkDescriptorSetLayout layout)
	{
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = _descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &layout;

		VkDescriptorSet descriptorSet;
		VkResult result = vkAllocateDescriptorSets(_device.GetDevice(), &allocInfo, &descriptorSet);

		if (result == VK_ERROR_OUT_OF_POOL_MEMORY)
		{
			throw runtime_error("Descriptor pool out of memory!");
		}
		else if (result == VK_ERROR_FRAGMENTED_POOL)
		{
			throw runtime_error("Descriptor pool fragmented!");
		}
		else if (result != VK_SUCCESS)
		{
			throw runtime_error("Failed to allocate descriptor set!");
		}

		return descriptorSet;
	}

	vector<VkDescriptorSet> DescriptorPool::AllocateDescriptorSets(
		const vector<VkDescriptorSetLayout>& layouts)
	{
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = _descriptorPool;
		allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
		allocInfo.pSetLayouts = layouts.data();

		vector<VkDescriptorSet> descriptorSets(layouts.size());
		VkResult result = vkAllocateDescriptorSets(_device.GetDevice(), &allocInfo, descriptorSets.data());

		if (result == VK_ERROR_OUT_OF_POOL_MEMORY)
		{
			throw runtime_error("Descriptor pool out of memory!");
		}
		else if (result == VK_ERROR_FRAGMENTED_POOL)
		{
			throw runtime_error("Descriptor pool fragmented!");
		}
		else if (result != VK_SUCCESS)
		{
			throw runtime_error("Failed to allocate descriptor sets!");
		}

		return descriptorSets;
	}
}
