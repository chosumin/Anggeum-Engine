#include "stdafx.h"
#include "BindlessTextureManager.h"
#include <stdexcept>

namespace Core
{
	BindlessTextureManager::BindlessTextureManager(Device& device, uint32_t maxTextures)
		: _device(device), _maxTextures(maxTextures)
	{
		_textureSlots.resize(maxTextures);
		_freeSlots.reserve(maxTextures);
		
		// Initialize all slots as free
		for (uint32_t i = 0; i < maxTextures; ++i)
		{
			_freeSlots.push_back(i);
		}
	}

	BindlessTextureManager::~BindlessTextureManager()
	{
		// Descriptor set is freed when pool is destroyed
		
		if (_descriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(_device.GetDevice(), _descriptorPool, nullptr);
		}
		
		if (_descriptorSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(_device.GetDevice(), _descriptorSetLayout, nullptr);
		}
	}

	void BindlessTextureManager::Initialize()
	{
		CreateDescriptorSetLayout();
		CreateDescriptorPool();
		AllocateDescriptorSet();
	}

	TextureHandle BindlessTextureManager::RegisterTexture(shared_ptr<Texture> texture)
	{
		if (!texture)
		{
			throw std::runtime_error("Cannot register null texture to bindless manager");
		}

		uint32_t slotIndex = AllocateSlot();
		
		auto& slot = _textureSlots[slotIndex];
		slot.texture = texture;
		slot.generation++;
		slot.isActive = true;
		
		_activeTextureCount++;
		_pendingUpdates.push_back(slotIndex);
		_needsUpdate = true;

		TextureHandle handle;
		handle.index = slotIndex;
		handle.generation = slot.generation;
		
		return handle;
	}

	void BindlessTextureManager::UnregisterTexture(TextureHandle handle)
	{
		if (!handle.IsValid() || handle.index >= _maxTextures)
			return;

		auto& slot = _textureSlots[handle.index];
		
		// Validate generation
		if (slot.generation != handle.generation || !slot.isActive)
			return;

		slot.texture.reset();
		slot.isActive = false;
		
		FreeSlot(handle.index);
		_activeTextureCount--;
		
		// Mark for update (set to null/default texture)
		_pendingUpdates.push_back(handle.index);
		_needsUpdate = true;
	}

	void BindlessTextureManager::UpdateTexture(TextureHandle handle, shared_ptr<Texture> texture)
	{
		if (!handle.IsValid() || handle.index >= _maxTextures || !texture)
			return;

		auto& slot = _textureSlots[handle.index];
		
		if (slot.generation != handle.generation || !slot.isActive)
			return;

		slot.texture = texture;
		
		_pendingUpdates.push_back(handle.index);
		_needsUpdate = true;
	}

	void BindlessTextureManager::UpdateDescriptorSet()
	{
		if (!_needsUpdate || _pendingUpdates.empty())
			return;

		vector<VkDescriptorImageInfo> imageInfos;
		vector<VkWriteDescriptorSet> writes;
		
		imageInfos.reserve(_pendingUpdates.size());
		writes.reserve(_pendingUpdates.size());

		for (uint32_t index : _pendingUpdates)
		{
			auto& slot = _textureSlots[index];
			
			VkDescriptorImageInfo imageInfo{};
			if (slot.isActive && slot.texture)
			{
				imageInfo = slot.texture->GetDescriptorImageInfo();
			}
			else
			{
				// Use default/null texture (VK_NULL_HANDLE is valid for partially bound)
				imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				imageInfo.imageView = VK_NULL_HANDLE;
				imageInfo.sampler = VK_NULL_HANDLE;
			}
			
			imageInfos.push_back(imageInfo);

			VkWriteDescriptorSet write{};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = _descriptorSet;
			write.dstBinding = 0; // Bindless array binding
			write.dstArrayElement = index;
			write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			write.descriptorCount = 1;
			write.pImageInfo = &imageInfos.back();
			
			writes.push_back(write);
		}

		vkUpdateDescriptorSets(_device.GetDevice(), 
			static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

		_pendingUpdates.clear();
		_needsUpdate = false;
	}

	shared_ptr<Texture> BindlessTextureManager::GetTexture(TextureHandle handle) const
	{
		if (!handle.IsValid() || handle.index >= _maxTextures)
			return nullptr;

		auto& slot = _textureSlots[handle.index];
		
		if (slot.generation != handle.generation || !slot.isActive)
			return nullptr;

		return slot.texture;
	}

	void BindlessTextureManager::CreateDescriptorSetLayout()
	{
		VkDescriptorSetLayoutBinding binding{};
		binding.binding = 0;
		binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		binding.descriptorCount = _maxTextures;
		binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
		binding.pImmutableSamplers = nullptr;

		// Enable descriptor indexing features
		VkDescriptorBindingFlags bindingFlags = 
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
			VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
			VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

		VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
		bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
		bindingFlagsInfo.bindingCount = 1;
		bindingFlagsInfo.pBindingFlags = &bindingFlags;

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = 1;
		layoutInfo.pBindings = &binding;
		layoutInfo.pNext = &bindingFlagsInfo;
		layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

		if (vkCreateDescriptorSetLayout(_device.GetDevice(), &layoutInfo, 
			nullptr, &_descriptorSetLayout) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create bindless descriptor set layout!");
		}
	}

	void BindlessTextureManager::CreateDescriptorPool()
	{
		VkDescriptorPoolSize poolSize{};
		poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSize.descriptorCount = _maxTextures;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = 1;
		poolInfo.pPoolSizes = &poolSize;
		poolInfo.maxSets = 1; // Single bindless set
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

		if (vkCreateDescriptorPool(_device.GetDevice(), &poolInfo, 
			nullptr, &_descriptorPool) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create bindless descriptor pool!");
		}
	}

	void BindlessTextureManager::AllocateDescriptorSet()
	{
		VkDescriptorSetVariableDescriptorCountAllocateInfo countInfo{};
		countInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
		countInfo.descriptorSetCount = 1;
		countInfo.pDescriptorCounts = &_maxTextures;

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = _descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &_descriptorSetLayout;
		allocInfo.pNext = &countInfo;

		if (vkAllocateDescriptorSets(_device.GetDevice(), &allocInfo, 
			&_descriptorSet) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to allocate bindless descriptor set!");
		}
	}

	uint32_t BindlessTextureManager::AllocateSlot()
	{
		if (_freeSlots.empty())
		{
			throw std::runtime_error("Bindless texture array is full! Maximum capacity: " + 
				std::to_string(_maxTextures));
		}

		uint32_t slot = _freeSlots.back();
		_freeSlots.pop_back();
		return slot;
	}

	void BindlessTextureManager::FreeSlot(uint32_t index)
	{
		_freeSlots.push_back(index);
	}
}