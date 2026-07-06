#include "stdafx.h"
#include "DescriptorSetBuilder.h"
#include "Shader.h"
#include "UniformBuffer.h"
#include "StorageBuffer.h"
#include "Buffer.h"
#include "Texture.h"
#include "Sampler.h"
#include "Image.h"

Core::DescriptorSetBuilder::DescriptorSetBuilder(
	Device& device, DescriptorPool& pool, Shader& shader, uint32_t setIndex)
	: _device(device), _pool(pool), _shader(shader), _setIndex(setIndex)
{
}

Core::DescriptorSetBuilder& Core::DescriptorSetBuilder::SetUniformBuffer(
	uint32_t binding, void* data)
{
	auto it = _resources.uniformBuffers.find(binding);
	if (it == _resources.uniformBuffers.end())
	{
		// Find buffer size from shader layout
		auto& layouts = _shader.GetDescriptorSetLayouts();
		auto layoutIt = layouts.find(_setIndex);
		if (layoutIt != layouts.end())
		{
			auto& uniformBindings = layoutIt->second->GetUniformBufferBindings();
			for (auto& bindingInfo : uniformBindings)
			{
				if (bindingInfo.Binding == binding)
				{
					auto buffer = new UniformBuffer(_device, bindingInfo.BufferSize);
					_resources.uniformBuffers[binding] = buffer;
					it = _resources.uniformBuffers.find(binding);
					break;
				}
			}
		}
	}

	if (it != _resources.uniformBuffers.end())
	{
		it->second->Update(data);
	}

	return *this;
}

Core::DescriptorSetBuilder& Core::DescriptorSetBuilder::SetUniformBuffer(
	uint32_t binding, UniformBuffer* buffer)
{
	// Caller owns this buffer — store pointer without ownership.
	// NOTE: CleanupBuffers will delete it, so caller must not delete separately,
	// or we need to track ownership. For now, treat as "builder owns all".
	_resources.uniformBuffers[binding] = buffer;
	return *this;
}

Core::DescriptorSetBuilder& Core::DescriptorSetBuilder::SetStorageBuffer(
	uint32_t binding, Buffer* buffer)
{
	auto it = _resources.storageBuffers.find(binding);
	if (it == _resources.storageBuffers.end())
	{
		auto storageBuffer = new StorageBuffer();
		_resources.storageBuffers[binding] = storageBuffer;
		it = _resources.storageBuffers.find(binding);
	}

	if (it != _resources.storageBuffers.end())
	{
		it->second->SetBuffer(buffer);
	}

	return *this;
}

Core::DescriptorSetBuilder& Core::DescriptorSetBuilder::SetTextureBuffer(
	uint32_t binding, shared_ptr<Texture> texture, uint32_t mipLevel,
	VkImageLayout layout)
{
	TextureBuffer texBuffer{};
	texBuffer.texture = texture;
	texBuffer.mipLevel = mipLevel;
	texBuffer.imageLayout = layout;

	_resources.textureBuffers[binding] = texBuffer;
	return *this;
}

Core::DescriptorSetResources& Core::DescriptorSetBuilder::Build()
{
	// 1. Allocate descriptor set
	auto& layouts = _shader.GetDescriptorSetLayouts();
	auto layoutIt = layouts.find(_setIndex);
	if (layoutIt == layouts.end())
		throw runtime_error("DescriptorSetBuilder: layout not found for set " + to_string(_setIndex));

	VkDescriptorSetLayout vkLayout = layoutIt->second->GetDescriptorSetLayout();
	_resources.descriptorSet = _pool.AllocateDescriptorSet(vkLayout);

	// Get valid bindings from shader layout
	auto& uniformBindings = layoutIt->second->GetUniformBufferBindings();
	auto& storageBindings = layoutIt->second->GetStorageBufferBindings();
	auto& textureBindings = layoutIt->second->GetTextureBufferBindings();

	// Build a set of valid binding numbers for each type
	unordered_set<uint32_t> validUniformBindings;
	unordered_set<uint32_t> validStorageBindings;
	unordered_set<uint32_t> validTextureBindings;

	for (const auto& b : uniformBindings)
		validUniformBindings.insert(b.Binding);
	for (const auto& b : storageBindings)
		validStorageBindings.insert(b.Binding);
	for (const auto& b : textureBindings)
		validTextureBindings.insert(b.Binding);

	// 2. Gather all writes (only for bindings that exist in the shader layout)
	vector<VkWriteDescriptorSet> writes;

	// Uniform buffers - only write if binding exists in shader layout
	for (auto& [binding, buffer] : _resources.uniformBuffers)
	{
		if (validUniformBindings.find(binding) == validUniformBindings.end())
			continue; // Skip bindings not in shader layout

		VkWriteDescriptorSet write = buffer->CreateWriteDescriptorSet(binding);
		write.dstSet = _resources.descriptorSet;
		writes.push_back(write);
	}

	// Storage buffers - only write if binding exists in shader layout
	for (auto& [binding, buffer] : _resources.storageBuffers)
	{
		if (validStorageBindings.find(binding) == validStorageBindings.end())
			continue; // Skip bindings not in shader layout

		VkWriteDescriptorSet write = buffer->CreateWriteDescriptorSet(binding);
		write.dstSet = _resources.descriptorSet;
		writes.push_back(write);
	}

	// Texture buffers - only write if binding exists in shader layout
	for (auto& [binding, texture] : _resources.textureBuffers)
	{
		if (validTextureBindings.find(binding) == validTextureBindings.end())
			continue; // Skip bindings not in shader layout

		VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

		// Find descriptor type from shader layout
		for (const auto& bindingInfo : textureBindings)
		{
			if (bindingInfo.Binding == binding)
			{
				descriptorType = bindingInfo.DescriptorType;
				break;
			}
		}

		VkWriteDescriptorSet write = texture.CreateWriteDescriptorSet(binding, descriptorType);
		write.dstSet = _resources.descriptorSet;
		writes.push_back(write);
	}

	// 3. Update
	if (!writes.empty())
	{
		vkUpdateDescriptorSets(
			_device.GetDevice(),
			static_cast<uint32_t>(writes.size()),
			writes.data(), 0, nullptr);
	}

	_resources.isDescriptorSetUpdated = true;
	return _resources;
}