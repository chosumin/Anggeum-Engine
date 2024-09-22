#include "stdafx.h"
#include "Material.h"
#include "VulkanWrapper/Shader.h"
#include "VulkanWrapper/CommandPool.h"
#include "VulkanWrapper/RenderTarget.h"
#include "ShaderFactory.h"

namespace Core
{
	Material::Material(Device& device, string shaderName, uint32_t hash)
		:_device(device), _isDirty(true), _hash(hash)
	{
		_shader = ShaderFactory::CreateShader(device, shaderName);

		CreateDescriptorSets();
		CreateBuffers();
	}

	Core::Material::~Material()
	{
		for (auto& texture : _textures)
		{
			delete(texture.second);
		}
		_textures.clear();

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
	}

	Shader& Core::Material::GetShader() const
	{
		return *_shader;
	}

	void Core::Material::SetShader(Shader& shader)
	{
	}

	void Core::Material::SetBuffer(uint32_t currentImage, uint32_t binding, void* data)
	{
		_uniformBuffers[binding]->SetBuffer(currentImage, data);
	}

	void Core::Material::SetBuffer(uint32_t binding, Texture* texture)
	{
		_textureBuffers[binding]->CopyDescriptorImageInfo(texture->GetDescriptorImageInfo());

		if (_textures[binding] != nullptr)
			delete(_textures[binding]);

		_textures[binding] = texture;
	}

	void Material::SetBuffer(uint32_t binding, RenderTarget* renderTarget)
	{
		_textureBuffers[binding]->CopyDescriptorImageInfo(renderTarget->GetDescriptorImageInfo());
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

			for (auto iter = _textureBuffers.begin(); iter != _textureBuffers.end(); ++iter)
			{
				VkWriteDescriptorSet writeDescriptorSet =
					iter->second->CreateWriteDescriptorSet(i, iter->first);

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
}