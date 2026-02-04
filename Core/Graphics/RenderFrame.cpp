#include "stdafx.h"
#include "RenderFrame.h"
#include "Material.h"
#include "Vulkans/UniformBuffer.h"
#include "Vulkans/Buffer.h"
#include "Vulkans/DescriptorPool.h"
#include "Vulkans/Shader.h"

using namespace Core;

RenderFrame::RenderFrame(Device& device, BindlessTextureManager* bindlessManager)
	: _device(device)
	, _bindlessTextureManager(bindlessManager)
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
	// Reset descriptor pool
	if (_descriptorPool)
	{
		_descriptorPool->Reset();
	}
	
	// Clear shader resources (set index 0)
	for (auto& [shaderHash, resources] : _shaderResources)
	{
		resources.CleanupBuffers();
	}
	_shaderResources.clear();
	
	// Clear material resources (set index 1)
	for (auto& [materialName, resources] : _materialResources)
	{
		resources.CleanupBuffers();
	}
	_materialResources.clear();
}

void RenderFrame::AllocateDescriptorSets(Shader& shader)
{
	auto shaderHash = shader.GetType();
	auto& resources = GetOrCreateShaderResources(shaderHash);

	auto& layouts = shader.GetDescriptorSetLayouts();

	auto layoutIt = layouts.find((uint)DescriptorSetType::Shader);
	if (layoutIt != layouts.end())
	{
		VkDescriptorSetLayout vkLayout = layoutIt->second->GetDescriptorSetLayout();

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = _descriptorPool->GetHandle();
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &vkLayout;

		VkResult result = vkAllocateDescriptorSets(_device.GetDevice(), &allocInfo, &resources.descriptorSet);

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
			throw runtime_error("Failed to allocate descriptor set for shader: " + shader.GetHash());
		}
	}
}

void RenderFrame::AllocateDescriptorSets(Material& material)
{
	auto materialName = material.GetName();
	auto& resources = GetOrCreateMaterialResources(materialName);

	auto& layouts = material.GetShader().GetDescriptorSetLayouts();

	auto layoutIt = layouts.find((uint)DescriptorSetType::Material);
	if (layoutIt != layouts.end())
	{
		VkDescriptorSetLayout vkLayout = layoutIt->second->GetDescriptorSetLayout();

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = _descriptorPool->GetHandle();
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &vkLayout;

		VkResult result = vkAllocateDescriptorSets(_device.GetDevice(), &allocInfo, &resources.descriptorSet);

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
			throw runtime_error("Failed to allocate descriptor set for material: " + material.GetName());
		}
	}
}

void RenderFrame::CleanupBuffers()
{
	// Cleanup shader resources (set index 0)
	for (auto& [shaderHash, resources] : _shaderResources)
	{
		resources.CleanupBuffers();
	}
	_shaderResources.clear();

	// Cleanup material resources (set index 1)
	for (auto& [materialName, resources] : _materialResources)
	{
		resources.CleanupBuffers();
	}
	_materialResources.clear();
}

// Per-shader resources access methods (set index 0)
DescriptorSetResources& RenderFrame::GetOrCreateShaderResources(size_t shaderHash)
{
	auto it = _shaderResources.find(shaderHash);
	if (it == _shaderResources.end())
	{
		// Create new shader resources
		it = _shaderResources.emplace(shaderHash, DescriptorSetResources{}).first;
	}
	return it->second;
}

DescriptorSetResources* RenderFrame::GetShaderResources(size_t shaderHash)
{
	auto it = _shaderResources.find(shaderHash);
	if (it == _shaderResources.end())
		return nullptr;
	return &(it->second);
}

// Per-material resources access methods (set index 1)
DescriptorSetResources& RenderFrame::GetOrCreateMaterialResources(const string& materialName)
{
	auto it = _materialResources.find(materialName);
	if (it == _materialResources.end())
	{
		// Create new material resources
		it = _materialResources.emplace(materialName, DescriptorSetResources{}).first;
	}
	return it->second;
}

DescriptorSetResources* RenderFrame::GetMaterialResources(const string& materialName)
{
	auto it = _materialResources.find(materialName);
	if (it == _materialResources.end())
		return nullptr;
	return &(it->second);
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

void RenderFrame::UpdateDescriptorSets(Shader& shader)
{
	// Get material resources from RenderFrame
	auto* resources = GetShaderResources(shader.GetHash());
	if (!resources)
		return;

	auto& descriptorSet = resources->descriptorSet;
	vector<VkWriteDescriptorSet> allDescriptorWrites;

	// Process uniform buffers
	for (auto& [binding, buffer] : resources->uniformBuffers)
	{
		VkWriteDescriptorSet writeDescriptorSet =
			buffer->CreateWriteDescriptorSet(binding);
		writeDescriptorSet.dstSet = descriptorSet;
		allDescriptorWrites.push_back(writeDescriptorSet);
	}

	// Process texture buffers - get textures from Material
	for (auto& [binding, textureBuffer] : resources->textureBuffers)
	{
		VkWriteDescriptorSet writeDescriptorSet =
			textureBuffer->CreateWriteDescriptorSet(binding);

		writeDescriptorSet.dstSet = descriptorSet;
		allDescriptorWrites.push_back(writeDescriptorSet);
	}

	// Process storage buffers
	for (auto& [binding, buffer] : resources->storageBuffers)
	{
		VkWriteDescriptorSet writeDescriptorSet =
			buffer->CreateWriteDescriptorSet(binding);

		writeDescriptorSet.dstSet = descriptorSet;
		allDescriptorWrites.push_back(writeDescriptorSet);
	}

	// Single batched update call
	if (!allDescriptorWrites.empty())
	{
		vkUpdateDescriptorSets(
			_device.GetDevice(),
			static_cast<uint32_t>(allDescriptorWrites.size()),
			allDescriptorWrites.data(), 0, nullptr);
	}
}

void RenderFrame::UpdateDescriptorSets(Material& material)
{
	// Get material resources from RenderFrame
	auto* resources = GetMaterialResources(material.GetName());
	if (!resources)
		return;

	auto& descriptorSet = resources->descriptorSet;
	vector<VkWriteDescriptorSet> allDescriptorWrites;

	// Process uniform buffers
	for (auto& [binding, buffer] : resources->uniformBuffers)
	{
		VkWriteDescriptorSet writeDescriptorSet =
			buffer->CreateWriteDescriptorSet(binding);
		writeDescriptorSet.dstSet = descriptorSet;
		allDescriptorWrites.push_back(writeDescriptorSet);
	}

	// Process texture buffers - get textures from Material
	for (auto& [binding, textureBuffer] : resources->textureBuffers)
	{
		VkWriteDescriptorSet writeDescriptorSet =
			textureBuffer->CreateWriteDescriptorSet(binding);

		writeDescriptorSet.dstSet = descriptorSet;
		allDescriptorWrites.push_back(writeDescriptorSet);
	}

	// Process storage buffers
	for (auto& [binding, buffer] : resources->storageBuffers)
	{
		VkWriteDescriptorSet writeDescriptorSet =
			buffer->CreateWriteDescriptorSet(binding);

		writeDescriptorSet.dstSet = descriptorSet;
		allDescriptorWrites.push_back(writeDescriptorSet);
	}

	// Single batched update call
	if (!allDescriptorWrites.empty())
	{
		vkUpdateDescriptorSets(
			_device.GetDevice(),
			static_cast<uint32_t>(allDescriptorWrites.size()),
			allDescriptorWrites.data(), 0, nullptr);
	}
}

// Per-shader buffer management methods (set index 0)
void RenderFrame::SetShaderUniformBuffer(Shader& shader, uint32_t binding, void* data)
{
	auto& resources = GetOrCreateShaderResources(shader.GetHash());
	
	// Find or create uniform buffer
	auto it = resources.uniformBuffers.find(binding);
	if (it == resources.uniformBuffers.end())
	{
		auto& layouts = shader.GetDescriptorSetLayouts();
		auto layoutIt = layouts.find(0); // Shader descriptor set (set index 0)
		if (layoutIt != layouts.end())
		{
			auto& uniformBindings = layoutIt->second->GetUniformBufferBindings();
			for (auto& bindingInfo : uniformBindings)
			{
				if (bindingInfo.Binding == binding)
				{
					auto buffer = new UniformBuffer(_device, bindingInfo.BufferSize);
					resources.uniformBuffers[binding] = buffer;
					it = resources.uniformBuffers.find(binding);
					break;
				}
			}
		}
	}

	if (it != resources.uniformBuffers.end())
	{
		it->second->Update(data);
	}
}

void RenderFrame::SetShaderTextureBuffer(Shader& shader, uint32_t binding, shared_ptr<Texture> texture)
{
	auto& resources = GetOrCreateShaderResources(shader.GetHash());
	
	// Get or create texture buffer
	auto it = resources.textureBuffers.find(binding);
	if (it == resources.textureBuffers.end())
	{
		auto buffer = new TextureBuffer();
		auto info = texture->GetDescriptorImageInfo();
		buffer->CopyDescriptorImageInfo(info);
		resources.textureBuffers[binding] = buffer;
	}
}

void RenderFrame::SetShaderStorageBuffer(Shader& shader, uint32_t binding, Buffer* buffer)
{
	auto& resources = GetOrCreateShaderResources(shader.GetHash());
	
	// Get or create storage buffer
	auto it = resources.storageBuffers.find(binding);
	if (it == resources.storageBuffers.end())
	{
		auto storageBuffer = new StorageBuffer();
		resources.storageBuffers[binding] = storageBuffer;
		it = resources.storageBuffers.find(binding);
	}
	
	if (it != resources.storageBuffers.end())
	{
		it->second->SetBuffer(buffer);
	}
}

// Per-material buffer management methods (set index 1)
void RenderFrame::SetMaterialUniformBuffer(Material& material, uint32_t binding, void* data)
{
	auto& resources = GetOrCreateMaterialResources(material.GetName());
	
	// Find or create uniform buffer
	auto it = resources.uniformBuffers.find(binding);
	if (it == resources.uniformBuffers.end())
	{
		auto& layouts = material.GetShader().GetDescriptorSetLayouts();
		auto layoutIt = layouts.find(1); // Material descriptor set (set index 1)
		if (layoutIt != layouts.end())
		{
			auto& uniformBindings = layoutIt->second->GetUniformBufferBindings();
			for (auto& bindingInfo : uniformBindings)
			{
				if (bindingInfo.Binding == binding)
				{
					auto buffer = new UniformBuffer(_device, bindingInfo.BufferSize);
					resources.uniformBuffers[binding] = buffer;
					it = resources.uniformBuffers.find(binding);
					break;
				}
			}
		}
	}

	if (it != resources.uniformBuffers.end())
	{
		it->second->Update(data);
	}
}

void RenderFrame::SetMaterialTextureBuffer(Material& material, uint32_t binding, shared_ptr<Texture> texture)
{
	auto& resources = GetOrCreateMaterialResources(material.GetName());
	
	// Get or create texture buffer
	auto it = resources.textureBuffers.find(binding);
	if (it == resources.textureBuffers.end())
	{
		auto buffer = new TextureBuffer();
		auto info = texture->GetDescriptorImageInfo();
		buffer->CopyDescriptorImageInfo(info);
		resources.textureBuffers[binding] = buffer;
	}
}

void RenderFrame::SetMaterialStorageBuffer(Material& material, uint32_t binding, Buffer* buffer)
{
	auto& resources = GetOrCreateMaterialResources(material.GetName());
	
	// Get or create storage buffer
	auto it = resources.storageBuffers.find(binding);
	if (it == resources.storageBuffers.end())
	{
		auto storageBuffer = new StorageBuffer();
		resources.storageBuffers[binding] = storageBuffer;
		it = resources.storageBuffers.find(binding);
	}
	
	if (it != resources.storageBuffers.end())
	{
		it->second->SetBuffer(buffer);
	}
}

void RenderFrame::SetMaterialBuffers(Material& material)
{
	// Process uniform buffers from Material
	auto& buffers = material.GetBuffersMap();
	for (auto& [binding, data] : buffers)
	{
		SetMaterialUniformBuffer(material, binding, data);
	}

	// Process texture buffers from Material
	auto& textures = material.GetTexturesMap();
	for (auto& [binding, texture] : textures)
	{
		SetMaterialTextureBuffer(material, binding, texture);
	}
}