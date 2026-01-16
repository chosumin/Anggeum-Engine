#include "stdafx.h"
#include "Graphics/Material.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/CommandPool.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/DescriptorPool.h"
#include "Graphics/ResourceCache.h"

namespace Core
{
	Material::Material(Device& device, string shaderName, string materialName)
		:_device(device), _isDirty(true), _name(materialName)
	{
		_shader = device.GetResourceCache().RequestShader(shaderName);

		// Initialize per-frame cache
		_cachedDescriptorSetsForBinding.resize(MAX_FRAMES_IN_FLIGHT);

		CreateDescriptorSets();
		CreateBuffers();

		//HACK : In case of empty textures. This should be replaced with the shader variants system later.
		SetDefault(device.GetResourceCache().RequestDefaultTexture());
	}

	Material::Material(Device& device, string materialName, string vertPath, string fragPath)
		:_device(device), _isDirty(true), _name(materialName)
	{
		_shader = device.GetResourceCache().RequestShader(vertPath, fragPath);

		CreateDescriptorSets();
		CreateBuffers();

		//HACK : In case of empty textures. This should be replaced with the shader variants system later.
		SetDefault(device.GetResourceCache().RequestDefaultTexture());
	}

	Material::Material(const Material& other)
		: _device(other._device),
		_shader(other._shader),
		_pushConstants(other._pushConstants),
		_uniformBuffers(other._uniformBuffers),
		_textureBuffers(other._textureBuffers),
		_isDirty(other._isDirty),
		_name(other._name),
		_descriptorSets(other._descriptorSets),
		_isDoubledSided(other._isDoubledSided),
		_alphaMode(other._alphaMode),
		_isAlphaCutoff(other._isAlphaCutoff),
		_buffers(other._buffers),
		_textures(other._textures)
	{
		// Initialize per-frame cache
		_cachedDescriptorSetsForBinding.resize(MAX_FRAMES_IN_FLIGHT);
	}

	Material& Material::operator=(const Material& other)
	{
		if (this == &other)
			return *this;

		_shader = other._shader;
		_pushConstants = other._pushConstants;
		_uniformBuffers = other._uniformBuffers;
		_textureBuffers = other._textureBuffers;
		_isDirty = other._isDirty;
		_name = other._name;
		_descriptorSets = other._descriptorSets;
		_isDoubledSided = other._isDoubledSided;
		_alphaMode = other._alphaMode;
		_isAlphaCutoff = other._isAlphaCutoff;
		_buffers = other._buffers;
		_textures = other._textures;

		return *this;
	}

	Core::Material::~Material()
	{
		for (auto& [setIndex, bindingMap] : _uniformBuffers)
		{
			for (auto& [binding, buffer] : bindingMap)
			{
				delete buffer;
			}
		}
		_uniformBuffers.clear();

		for (auto& [setIndex, bindingMap] : _textureBuffers)
		{
			for (auto& [binding, buffer] : bindingMap)
			{
				delete buffer;
			}
		}
		_textureBuffers.clear();

		for (auto& [setIndex, bindingMap] : _storageBuffers)
		{
			for (auto& [binding, buffer] : bindingMap)
			{
				delete buffer;
			}
		}
		_storageBuffers.clear();

		_buffers.clear();
		_textures.clear();
	}

	Shader& Core::Material::GetShader() const
	{
		return *_shader;
	}

	void Core::Material::SetBuffer(uint32_t setIndex, uint32_t currentImage, uint32_t binding, void* data)
	{
		auto setIt = _uniformBuffers.find(setIndex);
		if (setIt == _uniformBuffers.end())
			return;

		auto bindingIt = setIt->second.find(binding);
		if (bindingIt == setIt->second.end())
			return;

		bindingIt->second->SetBuffer(currentImage, data);
	}

	void Core::Material::SetBuffer(uint32_t setIndex, uint32_t binding, shared_ptr<Texture> texture)
	{
		auto setIt = _textureBuffers.find(setIndex);
		if (setIt == _textureBuffers.end())
			return;

		auto bindingIt = setIt->second.find(binding);
		if (bindingIt == setIt->second.end())
			return;

		_textures[setIndex][binding] = texture;
	}

	void Material::SetStorageBuffer(uint32_t setIndex, uint32_t currentImage, uint32_t binding, Buffer* buffer)
	{
		auto setIt = _storageBuffers.find(setIndex);
		if (setIt == _storageBuffers.end())
			return;

		auto bindingIt = setIt->second.find(binding);
		if (bindingIt == setIt->second.end())
			return;

		bindingIt->second->SetBuffer(currentImage, buffer);
	}

	void Material::SetStorageBuffer(uint32_t setIndex, uint32_t binding, Buffer* buffer)
	{
		auto setIt = _storageBuffers.find(setIndex);
		if (setIt == _storageBuffers.end())
			return;

		auto bindingIt = setIt->second.find(binding);
		if (bindingIt == setIt->second.end())
			return;

		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
			bindingIt->second->SetBuffer(i, buffer);
	}

	shared_ptr<Texture> Material::GetTexture(uint32_t setIndex, uint32_t binding)
	{
		auto setIt = _textures.find(setIndex);
		if (setIt != _textures.end())
		{
			auto bindingIt = setIt->second.find(binding);
			if (bindingIt != setIt->second.end())
			{
				return bindingIt->second;
			}
		}
		return nullptr;
	}

	void Core::Material::UpdateDescriptorSets()
	{
		uint32_t size = static_cast<uint32_t>(
			_uniformBuffers.size() + 
			_textureBuffers.size() + 
			_storageBuffers.size());

		// Collect all write operations first
		vector<VkWriteDescriptorSet> allDescriptorWrites;
		allDescriptorWrites.reserve(_descriptorSets.size() * MAX_FRAMES_IN_FLIGHT * size);

		for (auto& [setIndex, descriptorSetVector] : _descriptorSets)
		{
			for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
			{
				auto descriptorSet = descriptorSetVector[i];

				// Process uniform buffers for this set
				auto uniformSetIt = _uniformBuffers.find(setIndex);
				if (uniformSetIt != _uniformBuffers.end())
				{
					for (auto& [binding, buffer] : uniformSetIt->second)
					{
						VkWriteDescriptorSet writeDescriptorSet =
							buffer->CreateWriteDescriptorSet(i, binding);

						writeDescriptorSet.dstSet = descriptorSet;
						allDescriptorWrites.push_back(writeDescriptorSet);
					}
				}

				// Process texture buffers for this set
				auto textureSetIt = _textures.find(setIndex);
				auto textureBufferSetIt = _textureBuffers.find(setIndex);
				if (textureSetIt != _textures.end() && textureBufferSetIt != _textureBuffers.end())
				{
					for (auto& [binding, texture] : textureSetIt->second)
					{
						auto bufferIt = textureBufferSetIt->second.find(binding);
						if (bufferIt != textureBufferSetIt->second.end())
						{
							auto info = texture->GetDescriptorImageInfo();
							bufferIt->second->CopyDescriptorImageInfo(info);

							VkWriteDescriptorSet writeDescriptorSet =
								bufferIt->second->CreateWriteDescriptorSet(i, binding);

							writeDescriptorSet.dstSet = descriptorSet;
							allDescriptorWrites.push_back(writeDescriptorSet);
						}
					}
				}

				// Process storage buffers for this set
				auto storageSetIt = _storageBuffers.find(setIndex);
				if (storageSetIt != _storageBuffers.end())
				{
					for (auto& [binding, buffer] : storageSetIt->second)
					{
						VkWriteDescriptorSet writeDescriptorSet =
							buffer->CreateWriteDescriptorSet(i, binding);

						writeDescriptorSet.dstSet = descriptorSet;
						allDescriptorWrites.push_back(writeDescriptorSet);
					}
				}
			}
		}

		// Single batched update call
		if (!allDescriptorWrites.empty())
		{
			vkUpdateDescriptorSets(
				_device.GetDevice(),
				static_cast<uint32_t>(allDescriptorWrites.size()),
				allDescriptorWrites.data(), 0, nullptr);
		}

		_isDirty = false;
	}

	vector<uint8_t>* Material::GetPushConstantsData()
	{
		return &_pushConstants;
	}

	void Material::ClearPushConstantsCache()
	{
		_pushConstants.clear();
	}

	void Material::CreateDescriptorSets()
	{
		// Get all descriptor set layouts from shader
		auto& layouts = _shader->GetDescriptorSetLayouts();
		
		// Allocate descriptor sets for each set index
		for (auto& [setIndex, descriptorLayout] : layouts)
		{
			VkDescriptorSetLayout vkLayout = descriptorLayout->GetDescriptorSetLayout();
			vector<VkDescriptorSetLayout> perFrameLayouts(MAX_FRAMES_IN_FLIGHT, vkLayout);
			
			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = _device.GetGlobalDescriptorPool();
			allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
			allocInfo.pSetLayouts = perFrameLayouts.data();

			_descriptorSets[setIndex].resize(MAX_FRAMES_IN_FLIGHT);
			
			VkResult result = vkAllocateDescriptorSets(_device.GetDevice(),
				&allocInfo, _descriptorSets[setIndex].data());

			if (result != VK_SUCCESS)
			{
				if (result == VK_ERROR_OUT_OF_POOL_MEMORY)
				{
					throw runtime_error("Global descriptor pool out of memory! Increase pool size.");
				}
				else if (result == VK_ERROR_FRAGMENTED_POOL)
				{
					throw runtime_error("Global descriptor pool fragmented!");
				}
				throw runtime_error("Failed to allocate descriptor sets for set index " + std::to_string(setIndex));
			}
		}
	}

	void Material::CreateBuffers()
	{
		auto& layouts = _shader->GetDescriptorSetLayouts();
		
		// Iterate through all descriptor set layouts
		for (auto& [setIndex, descriptorLayout] : layouts)
		{
			// Create uniform buffers
			auto& uniformBindings = descriptorLayout->GetUniformBufferBindings();
			for (auto& binding : uniformBindings)
			{
				auto buffer = new Core::UniformBuffer(_device, binding.BufferSize);
				_uniformBuffers[setIndex][binding.Binding] = buffer;
			}

			// Create texture buffers
			auto& textureBindings = descriptorLayout->GetTextureBufferBindings();
			for (auto& binding : textureBindings)
			{
				auto buffer = new Core::TextureBuffer();
				_textureBuffers[setIndex][binding.Binding] = buffer;
			}

			// Create storage buffers
			auto& storageBindings = descriptorLayout->GetStorageBufferBindings();
			for (auto& binding : storageBindings)
			{
				auto buffer = new Core::StorageBuffer();
				_storageBuffers[setIndex][binding.Binding] = buffer;
			}
		}
	}

	void Material::SetDefault(shared_ptr<Texture> defaultTexture)
	{
		auto descriptor = defaultTexture->GetDescriptorImageInfo();

		for (auto& [setIndex, bindingMap] : _textureBuffers)
		{
			for (auto& [binding, textureBuffer] : bindingMap)
			{
				textureBuffer->CopyDescriptorImageInfo(descriptor);
				_textures[setIndex][binding] = defaultTexture;
			}
		}
	}
}