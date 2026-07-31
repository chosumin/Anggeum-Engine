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
	uint32_t binding, Buffer& buffer)
{
	// UniformBuffer is a non-owning view; `buffer` stays owned by the caller.
	_resources.uniformBuffers[binding].SetBuffer(&buffer);
	return *this;
}

Core::DescriptorSetBuilder& Core::DescriptorSetBuilder::SetStorageBuffer(
	uint32_t binding, Buffer& buffer)
{
	// StorageBuffer is a non-owning view; `buffer` stays owned by the caller.
	_resources.storageBuffers[binding].SetBuffer(buffer);
	return *this;
}

Core::DescriptorSetBuilder& Core::DescriptorSetBuilder::SetTextureBuffer(
	uint32_t binding, Handle<Texture> texture, uint32_t mipLevel,
	VkImageLayout layout)
{
	TextureBuffer texBuffer{};
	texBuffer.texture = texture;
	texBuffer.mipLevel = mipLevel;
	texBuffer.imageLayout = layout;

	_resources.textureBuffers[binding] = texBuffer;
	return *this;
}

Core::DescriptorSetBuilder& Core::DescriptorSetBuilder::SetTextureBuffer(
	uint32_t binding, Texture& texture, uint32_t mipLevel,
	VkImageLayout layout)
{
	TextureBuffer texBuffer{};
	texBuffer.rawTexture = &texture;
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
	_resources.setIndex = _setIndex;

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

		VkWriteDescriptorSet write = buffer.CreateWriteDescriptorSet(binding);
		write.dstSet = _resources.descriptorSet;
		writes.push_back(write);
	}

	// Storage buffers - only write if binding exists in shader layout
	for (auto& [binding, buffer] : _resources.storageBuffers)
	{
		if (validStorageBindings.find(binding) == validStorageBindings.end())
			continue; // Skip bindings not in shader layout

		VkWriteDescriptorSet write = buffer.CreateWriteDescriptorSet(binding);
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