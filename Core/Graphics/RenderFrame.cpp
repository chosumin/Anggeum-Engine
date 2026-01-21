#include "stdafx.h"
#include "RenderFrame.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/DescriptorPool.h"
#include "Graphics/Vulkans/UniformBuffer.h"
#include "Graphics/Vulkans/TextureBuffer.h"
#include "Graphics/Vulkans/StorageBuffer.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Material.h"

namespace Core
{
	RenderFrame::RenderFrame(Device& device)
		: _device(device)
	{
		CreateSyncObjects();
		CreateDescriptorPool();
	}

	RenderFrame::~RenderFrame()
	{
		CleanupBuffers();

		if (_imageAvailableSemaphore != VK_NULL_HANDLE)
		{
			vkDestroySemaphore(_device.GetDevice(), _imageAvailableSemaphore, nullptr);
		}
		if (_renderFinishedSemaphore != VK_NULL_HANDLE)
		{
			vkDestroySemaphore(_device.GetDevice(), _renderFinishedSemaphore, nullptr);
		}
	}

	void RenderFrame::Reset()
	{
		// Clean up buffers from previous frame
		//CleanupBuffers();

		// Reset descriptor pool
		if (_descriptorPool)
		{
			_descriptorPool->Reset();
		}
		
		// Clear descriptor sets and update tracking
		_descriptorSets.clear();
		_cachedDescriptorSets.clear();
		_updatedDescriptorSets.clear();
	}

	UniformBuffer* RenderFrame::CreateUniformBuffer(VkDeviceSize size)
	{
		auto buffer = new UniformBuffer(_device, size);
		_uniformBuffers.push_back(buffer);
		return buffer;
	}

	TextureBuffer* RenderFrame::CreateTextureBuffer()
	{
		auto buffer = new TextureBuffer();
		_textureBuffers.push_back(buffer);
		return buffer;
	}

	StorageBuffer* RenderFrame::CreateStorageBuffer()
	{
		auto buffer = new StorageBuffer();
		_storageBuffers.push_back(buffer);
		return buffer;
	}

	void RenderFrame::CleanupBuffers()
	{
		// Delete all uniform buffers
		for (auto buffer : _uniformBuffers)
		{
			delete buffer;
		}
		_uniformBuffers.clear();

		// Delete all texture buffers
		for (auto buffer : _textureBuffers)
		{
			delete buffer;
		}
		_textureBuffers.clear();

		// Delete all storage buffers
		for (auto buffer : _storageBuffers)
		{
			delete buffer;
		}
		_storageBuffers.clear();
	}

	unordered_map<uint32_t, VkDescriptorSet>& RenderFrame::GetOrCreateDescriptorSets(Material& material)
	{
		auto materialName = material.GetName();
		
		// Check if descriptor sets already exist for this material
		auto materialIt = _descriptorSets.find(materialName);
		if (materialIt != _descriptorSets.end())
		{
			return materialIt->second;
		}
		
		// Get all descriptor set layouts from material's shader
		auto& shader = material.GetShader();
		auto& layouts = shader.GetDescriptorSetLayouts();

		// Allocate descriptor sets for each set index
		auto& materialDescriptorSets = _descriptorSets[materialName];

		for (auto& [setIndex, descriptorLayout] : layouts)
		{
			VkDescriptorSetLayout vkLayout = descriptorLayout->GetDescriptorSetLayout();

			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = _descriptorPool->GetHandle();
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &vkLayout;

			VkDescriptorSet descriptorSet;
			VkResult result = vkAllocateDescriptorSets(_device.GetDevice(), &allocInfo, &descriptorSet);

			if (result != VK_SUCCESS)
			{
				if (result == VK_ERROR_OUT_OF_POOL_MEMORY)
				{
					throw runtime_error("Descriptor pool out of memory! Increase pool size.");
				}
				else if (result == VK_ERROR_FRAGMENTED_POOL)
				{
					throw runtime_error("Descriptor pool fragmented!");
				}
				throw runtime_error("Failed to allocate descriptor set for set index " + std::to_string(setIndex));
			}

			materialDescriptorSets[setIndex] = descriptorSet;
		}

		return materialDescriptorSets;
	}

	void RenderFrame::CreateSyncObjects()
	{
		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		if (vkCreateSemaphore(_device.GetDevice(), &semaphoreInfo, nullptr, &_imageAvailableSemaphore) != VK_SUCCESS ||
			vkCreateSemaphore(_device.GetDevice(), &semaphoreInfo, nullptr, &_renderFinishedSemaphore) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to create semaphores for a frame!");
		}
	}

	void RenderFrame::CreateDescriptorPool()
	{
		_descriptorPool = make_unique<DescriptorPool>(_device);
		
		// Define pool sizes for different descriptor types
		vector<VkDescriptorPoolSize> poolSizes = {
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 }
		};
		
		// Create pool with sufficient descriptor sets
		uint32_t maxSets = 1000;
		_descriptorPool->CreatePool(poolSizes, maxSets);
	}

	const vector<VkDescriptorSet>& RenderFrame::GetDescriptorSetsForBinding(
		Material& material,
		const vector<uint32_t>& setIndices)
	{
		auto materialName = material.GetName();
		auto& cache = _cachedDescriptorSets[materialName];

		cache.clear();
		cache.reserve(setIndices.size());

		auto& materialSets = _descriptorSets[materialName];
		for (auto setIndex : setIndices)
		{
			auto it = materialSets.find(setIndex);
			if (it != materialSets.end())
			{
				cache.push_back(it->second);
			}
		}

		return cache;
	}

	bool RenderFrame::IsDescriptorSetUpdated(const string& materialName) const
	{
		return _updatedDescriptorSets.find(materialName) != _updatedDescriptorSets.end();
	}

	void RenderFrame::MarkDescriptorSetUpdated(const string& materialName)
	{
		_updatedDescriptorSets.insert(materialName);
	}
}