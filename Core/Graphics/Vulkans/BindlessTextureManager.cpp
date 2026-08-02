#include "stdafx.h"
#include "BindlessTextureManager.h"

namespace Core
{
	BindlessTextureManager::BindlessTextureManager(Device& device, uint32_t maxTextures)
		: _device(device), _maxTextures(maxTextures)
	{
		_texture2DSlots.resize(maxTextures);
		_cubemapSlots.resize(maxTextures);
		
		_freeTexture2DSlots.reserve(maxTextures);
		_freeCubemapSlots.reserve(maxTextures);
		
		// Initialize all slots as free
		for (uint32_t i = 0; i < maxTextures; ++i)
		{
			_freeTexture2DSlots.push_back(i);
			_freeCubemapSlots.push_back(i);
		}

		CreateDescriptorSetLayout();
		CreateDescriptorPool();
		AllocateDescriptorSet();

		cout << "Bindless texture system initialized with "
			<< _maxTextures << " slots" << endl;
	}

	BindlessTextureManager::~BindlessTextureManager()
	{
		if (_descriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(_device.GetDevice(), _descriptorPool, nullptr);
		}
		
		if (_descriptorSetLayout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(_device.GetDevice(), _descriptorSetLayout, nullptr);
		}
	}

	uint32_t BindlessTextureManager::RegisterTexture(Handle<Texture> texture)
	{
		Texture* resolved = texture.TryGet();
		if (!resolved)
		{
			throw runtime_error("Cannot register null texture to bindless manager");
		}

		// Detect if cubemap or 2D
		bool isCubemap = resolved->GetLayers() == 6;

		uint32_t slotIndex = AllocateSlot(isCubemap);

		auto& slot = isCubemap ? _cubemapSlots[slotIndex] : _texture2DSlots[slotIndex];
		slot.texture = texture;
		slot.isActive = true;

		if (isCubemap)
			_activeCubemapCount++;
		else
			_activeTexture2DCount++;

		uint32_t bindlessIndex = slotIndex | (isCubemap ? BindlessCubemapFlag : 0);
		_pendingUpdates.push_back(bindlessIndex);
		_needsUpdate = true;

		return bindlessIndex;
	}

	void BindlessTextureManager::UnregisterTexture(uint32_t bindlessIndex)
	{
		if (bindlessIndex == InvalidBindlessIndex)
			return;

		bool isCubemap = (bindlessIndex & BindlessCubemapFlag) != 0;
		uint32_t slotIndex = bindlessIndex & ~BindlessCubemapFlag;

		if (slotIndex >= _maxTextures)
			return;

		auto& slot = isCubemap ? _cubemapSlots[slotIndex] : _texture2DSlots[slotIndex];

		if (!slot.isActive)
			return;

		slot.texture = Handle<Texture>{};
		slot.textureBuffer.mipLevel = 0;
		slot.isActive = false;

		FreeSlot(slotIndex, isCubemap);

		if (isCubemap)
			_activeCubemapCount--;
		else
			_activeTexture2DCount--;

		_pendingUpdates.push_back(bindlessIndex);
		_needsUpdate = true;
	}

	void BindlessTextureManager::Sync()
	{
		if (!_needsUpdate || _pendingUpdates.empty())
			return;

		vector<VkWriteDescriptorSet> writes;
		writes.reserve(_pendingUpdates.size());

		// Use default/null texture (VK_NULL_HANDLE is valid for partially bound)
		VkDescriptorImageInfo defaultImageInfo{};
		defaultImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		defaultImageInfo.imageView = VK_NULL_HANDLE;
		defaultImageInfo.sampler = VK_NULL_HANDLE;

		for (uint32_t packedIndex : _pendingUpdates)
		{
			bool isCubemap = (packedIndex & BindlessCubemapFlag) != 0;
			uint32_t slotIndex = packedIndex & ~BindlessCubemapFlag;
			
			auto& slot = isCubemap ? _cubemapSlots[slotIndex] : _texture2DSlots[slotIndex];
			
			uint binding = isCubemap ? 1 : 0;

			VkDescriptorImageInfo imageInfo{};
			if (slot.isActive && slot.texture.IsValid())
			{
				slot.textureBuffer.rawTexture = &slot.texture.Get();
				auto write = slot.textureBuffer.CreateWriteDescriptorSet(binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
				write.dstSet = _descriptorSet;
				write.dstArrayElement = slotIndex;
				writes.push_back(write);
			}
			else
			{
				VkWriteDescriptorSet write{};
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.dstSet = _descriptorSet;
				write.dstBinding = binding;
				write.dstArrayElement = slotIndex;
				write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				write.descriptorCount = 1;
				write.pImageInfo = &defaultImageInfo;

				writes.push_back(write);
			}
		}

		vkUpdateDescriptorSets(_device.GetDevice(), 
			static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

		_pendingUpdates.clear();
		_needsUpdate = false;
	}

	void BindlessTextureManager::CreateDescriptorSetLayout()
	{
		array<VkDescriptorSetLayoutBinding, 2> bindings{};
		
		// Binding 0: 2D texture array
		bindings[0].binding = 0;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[0].descriptorCount = _maxTextures;
		bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
		bindings[0].pImmutableSamplers = nullptr;

		// Binding 1: Cubemap texture array
		bindings[1].binding = 1;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[1].descriptorCount = _maxTextures;
		bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
		bindings[1].pImmutableSamplers = nullptr;

		// Only apply VARIABLE_DESCRIPTOR_COUNT_BIT to the last binding
		array<VkDescriptorBindingFlags, 2> bindingFlags = {
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
			VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
			// Removed VARIABLE_DESCRIPTOR_COUNT_BIT from binding 0
			
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
			VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
			VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT
			// VARIABLE_DESCRIPTOR_COUNT_BIT only on last binding (binding 1)
		};

		VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
		bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
		bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
		bindingFlagsInfo.pBindingFlags = bindingFlags.data();

		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
		layoutInfo.pBindings = bindings.data();
		layoutInfo.pNext = &bindingFlagsInfo;
		layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

		if (vkCreateDescriptorSetLayout(_device.GetDevice(), &layoutInfo, 
			nullptr, &_descriptorSetLayout) != VK_SUCCESS)
		{
			throw runtime_error("Failed to create bindless descriptor set layout!");
		}
	}

	void BindlessTextureManager::CreateDescriptorPool()
	{
		array<VkDescriptorPoolSize, 2> poolSizes{};
		
		// 2D textures
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[0].descriptorCount = _maxTextures;
		
		// Cubemap textures
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[1].descriptorCount = _maxTextures;

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();
		poolInfo.maxSets = 1;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;

		if (vkCreateDescriptorPool(_device.GetDevice(), &poolInfo, 
			nullptr, &_descriptorPool) != VK_SUCCESS)
		{
			throw runtime_error("Failed to create bindless descriptor pool!");
		}
	}

	void BindlessTextureManager::AllocateDescriptorSet()
	{
		// Only specify variable count for the last binding (binding 1)
		uint32_t variableCount = _maxTextures;
		
		VkDescriptorSetVariableDescriptorCountAllocateInfo countInfo{};
		countInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
		countInfo.descriptorSetCount = 1;
		countInfo.pDescriptorCounts = &variableCount; // Only one count value

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = _descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &_descriptorSetLayout;
		allocInfo.pNext = &countInfo;

		if (vkAllocateDescriptorSets(_device.GetDevice(), &allocInfo, 
			&_descriptorSet) != VK_SUCCESS)
		{
			throw runtime_error("Failed to allocate bindless descriptor set!");
		}
	}

	uint32_t BindlessTextureManager::AllocateSlot(bool isCubemap)
	{
		auto& freeSlots = isCubemap ? _freeCubemapSlots : _freeTexture2DSlots;
		
		if (freeSlots.empty())
		{
			throw runtime_error("Bindless texture array is full! Maximum capacity: " + 
				to_string(_maxTextures));
		}

		uint32_t slot = freeSlots.back();
		freeSlots.pop_back();
		return slot;
	}

	void BindlessTextureManager::FreeSlot(uint32_t index, bool isCubemap)
	{
		auto& freeSlots = isCubemap ? _freeCubemapSlots : _freeTexture2DSlots;
		freeSlots.push_back(index);
	}
}