#include "stdafx.h"
#include "Graphics/Material.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/CommandPool.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/DescriptorPool.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/ResourceCache.h"

namespace Core
{
	Material::Material(Device& device, string shaderName, string materialName)
		:_device(device), _isDirty(true), _name(materialName)
	{
		_shader = device.GetResourceCache().RequestShader(shaderName);

		//HACK : In case of empty textures. This should be replaced with the shader variants system later.
		SetDefault(device.GetResourceCache().RequestDefaultTexture());
	}

	Material::Material(Device& device, string materialName, string vertPath, string fragPath)
		:_device(device), _isDirty(true), _name(materialName)
	{
		_shader = device.GetResourceCache().RequestShader(vertPath, fragPath);

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
		_isDoubledSided(other._isDoubledSided),
		_alphaMode(other._alphaMode),
		_isAlphaCutoff(other._isAlphaCutoff),
		_buffers(other._buffers),
		_textures(other._textures)
	{
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
		_isDoubledSided = other._isDoubledSided;
		_alphaMode = other._alphaMode;
		_isAlphaCutoff = other._isAlphaCutoff;
		_buffers = other._buffers;
		_textures = other._textures;

		return *this;
	}

	Core::Material::~Material()
	{
		// Buffers are now owned by RenderFrame, so we don't delete them here
		_uniformBuffers.clear();
		_textureBuffers.clear();
		_storageBuffers.clear();
		_buffers.clear();
		_textures.clear();
	}

	Shader& Core::Material::GetShader() const
	{
		return *_shader;
	}

	void Core::Material::SetBuffer(RenderFrame& frame, uint32_t setIndex, uint32_t currentImage, uint32_t binding, void* data)
	{
		auto& bindingMap = _uniformBuffers[setIndex];
		auto it = bindingMap.find(binding);
		
		if (it == bindingMap.end())
		{
			auto& layouts = _shader->GetDescriptorSetLayouts();
			auto layoutIt = layouts.find(setIndex);
			if (layoutIt != layouts.end())
			{
				auto& uniformBindings = layoutIt->second->GetUniformBufferBindings();
				for (auto& bindingInfo : uniformBindings)
				{
					if (bindingInfo.Binding == binding)
					{
						it = bindingMap.emplace(binding, frame.CreateUniformBuffer(bindingInfo.BufferSize)).first;
						break;
					}
				}
			}
		}

		if (it != bindingMap.end())
		{
			it->second->SetBuffer(data);
		}
	}

	void Core::Material::SetBuffer(RenderFrame& frame, uint32_t setIndex, uint32_t binding, shared_ptr<Texture> texture)
	{
		auto setIt = _textureBuffers.find(setIndex);
		if (setIt == _textureBuffers.end() || setIt->second.find(binding) == setIt->second.end())
		{
			// Create texture buffer through RenderFrame if not exists
			auto buffer = frame.CreateTextureBuffer();
			_textureBuffers[setIndex][binding] = buffer;
		}

		_textures[setIndex][binding] = texture;
	}

	void Material::SetStorageBuffer(RenderFrame& frame, uint32_t setIndex, uint32_t currentImage, uint32_t binding, Buffer* buffer)
	{
		auto setIt = _storageBuffers.find(setIndex);
		if (setIt == _storageBuffers.end() || setIt->second.find(binding) == setIt->second.end())
		{
			// Create storage buffer through RenderFrame if not exists
			auto storageBuffer = frame.CreateStorageBuffer();
			_storageBuffers[setIndex][binding] = storageBuffer;
		}

		auto bindingIt = _storageBuffers[setIndex].find(binding);
		if (bindingIt != _storageBuffers[setIndex].end())
		{
			bindingIt->second->SetBuffer(buffer);
		}
	}

	void Material::SetStorageBuffer(RenderFrame& frame, uint32_t setIndex, uint32_t binding, Buffer* buffer)
	{
		auto setIt = _storageBuffers.find(setIndex);
		if (setIt == _storageBuffers.end() || setIt->second.find(binding) == setIt->second.end())
		{
			// Create storage buffer through RenderFrame if not exists
			auto storageBuffer = frame.CreateStorageBuffer();
			_storageBuffers[setIndex][binding] = storageBuffer;
		}

		auto bindingIt = _storageBuffers[setIndex].find(binding);
		if (bindingIt != _storageBuffers[setIndex].end())
		{
			for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
			{
				bindingIt->second->SetBuffer(buffer);
			}
		}
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

	//TODO: fix the function to accept descriptor sets from outside.
	void Core::Material::UpdateDescriptorSets(unordered_map<uint32_t, VkDescriptorSet> descriptorSets)
	{
		uint32_t size = static_cast<uint32_t>(
			_uniformBuffers.size() +
			_textureBuffers.size() +
			_storageBuffers.size());

		// Collect all write operations first
		vector<VkWriteDescriptorSet> allDescriptorWrites;
		allDescriptorWrites.reserve(descriptorSets.size() * size);

		for (auto& [setIndex, descriptorSet] : descriptorSets)
		{
			// Process uniform buffers for this set
			auto uniformSetIt = _uniformBuffers.find(setIndex);
			if (uniformSetIt != _uniformBuffers.end())
			{
				// uniform buffers
				for (auto& [binding, buffer] : uniformSetIt->second)
				{
					VkWriteDescriptorSet writeDescriptorSet =
						buffer->CreateWriteDescriptorSet(binding);
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
							bufferIt->second->CreateWriteDescriptorSet(binding);
						
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
						buffer->CreateWriteDescriptorSet(binding);

					writeDescriptorSet.dstSet = descriptorSet;
					allDescriptorWrites.push_back(writeDescriptorSet);
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

	void Material::SetDefault(shared_ptr<Texture> defaultTexture)
	{
		auto& layouts = _shader->GetDescriptorSetLayouts();
		
		// Iterate through all descriptor set layouts
		for (auto& [setIndex, descriptorLayout] : layouts)
		{
			// Get texture bindings from the shader
			auto& textureBindings = descriptorLayout->GetTextureBufferBindings();
			for (auto& binding : textureBindings)
			{
				// Set default texture for each texture binding
				_textures[setIndex][binding.Binding] = defaultTexture;
			}
		}
	}
}