#include "stdafx.h"
#include "Graphics/Material.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/CommandPool.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/ResourceCache.h"

namespace Core
{
	Material::Material(Device& device, string shaderName, string materialName)
		:_device(device), _isDirty(true), _name(materialName)
	{
		_shader = device.GetResourceCache().RequestShader(shaderName);

		CreateDescriptorSets();
		CreateBuffers();

		//HACK : In case of empty textures. This should be replaced with the shader variants system later.
		SetDefault(*device.GetResourceCache().RequestDefaultTexture());
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
		for (auto& uniformBuffer : _uniformBuffers)
		{
			delete(uniformBuffer.second);
		}
		_uniformBuffers.clear();

		for (auto& textureBuffer : _textureBuffers)
		{
			delete(textureBuffer.second);
		}
		_textureBuffers.clear();

		for (auto& buffer : _buffers)
		{
			delete(buffer.second);
		}
		_buffers.clear();

		_textures.clear();
	}

	Shader& Core::Material::GetShader() const
	{
		return *_shader;
	}

	void Core::Material::SetBuffer(uint32_t currentImage, uint32_t binding, void* data)
	{
		if (_uniformBuffers.find(binding) == _uniformBuffers.end())
			return;

		_uniformBuffers[binding]->SetBuffer(currentImage, data);
	}

	void Core::Material::SetBuffer(uint32_t binding, shared_ptr<Texture> texture)
	{
		if (_textureBuffers.find(binding) == _textureBuffers.end())
			return;

		_textures[binding] = texture;
	}

	shared_ptr<Texture> Material::GetTexture(uint32_t binding)
	{
		auto it = _textures.find(binding);
		if (it != _textures.end())
		{
			return it->second;
		}
		return nullptr;
	}

	void Core::Material::UpdateDescriptorSets()
	{
		uint32_t size = static_cast<uint32_t>(_uniformBuffers.size() + _textureBuffers.size());

		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			vector<VkWriteDescriptorSet> descriptorWrites(size);
			
			uint32_t index = 0;

			auto descriptorSet = _descriptorSets[i];

			for (auto iter = _uniformBuffers.begin(); iter != _uniformBuffers.end(); ++iter)
			{
				VkWriteDescriptorSet writeDescriptorSet =
					iter->second->CreateWriteDescriptorSet(i, iter->first);

				descriptorWrites[index] = writeDescriptorSet;
				descriptorWrites[index].dstSet = descriptorSet;
				++index;
			}

			for (auto iter = _textures.begin(); iter != _textures.end(); ++iter)
			{
				auto info = iter->second->GetDescriptorImageInfo();
				_textureBuffers[iter->first]->CopyDescriptorImageInfo(info);

				VkWriteDescriptorSet writeDescriptorSet =
					_textureBuffers[iter->first]->CreateWriteDescriptorSet(i, iter->first);

				descriptorWrites[index] = writeDescriptorSet;
				descriptorWrites[index].dstSet = descriptorSet;
				++index;
			}

			vkUpdateDescriptorSets(
				_device.GetDevice(),
				static_cast<uint32_t>(descriptorWrites.size()),
				descriptorWrites.data(), 0, nullptr);
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
		vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, _shader->GetDescriptorSetLayout());
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = _shader->GetDescriptorPool();
		allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
		allocInfo.pSetLayouts = layouts.data();

		_descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
		if (vkAllocateDescriptorSets(_device.GetDevice(),
			&allocInfo, _descriptorSets.data()) != VK_SUCCESS)
		{
			throw runtime_error("failed to allocate descriptor sets!");
		}
	}

	void Material::CreateBuffers()
	{
		auto& uniformBindings = _shader->GetUniformBufferLayoutBindings();

		for (auto& binding : uniformBindings)
		{
			auto buffer = new Core::UniformBuffer(_device, binding.BufferSize);
			_uniformBuffers[binding.Binding] = buffer;
		}

		auto& textureBindings = _shader->GetTextureBufferLayoutBindings();
		
		for (auto& binding : textureBindings)
		{
			auto buffer = new Core::TextureBuffer();
			_textureBuffers[binding.Binding] = buffer;
		}
	}

	void Material::SetDefault(Texture& defaultTexture)
	{
		auto descriptor = defaultTexture.GetDescriptorImageInfo();

		for (auto&& textureBuffer : _textureBuffers)
		{
			textureBuffer.second->CopyDescriptorImageInfo(descriptor);

		}
	}
}