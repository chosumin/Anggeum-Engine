#include "stdafx.h"
#include "Material.h"
#include "Shader.h"
#include "CommandPool.h"
#include "ShaderFactory.h"
#include "PerspectiveCamera.h"

namespace Core
{
	Material::Material(Device& device, string shaderName)
		:_device(device)
	{
		_shader = ShaderFactory::CreateShader(device, shaderName);

		CreateDescriptorSets();
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

	void Material::SetTexture(uint32_t binding, Texture& texture)
	{
		//todo : set buffer
		//todo : UpdateDescriptorSets() before begin render pass.
	}

	void Core::Material::SetBuffer(uint32_t currentImage, uint32_t binding, void* data)
	{
		_uniformBuffers[binding]->SetBuffer(currentImage, data);
	}

	void Core::Material::SetBuffer(uint32_t binding, Texture* texture)
	{
		_textureBuffers[binding]->SetDescriptorImageInfo(texture->GetDescriptorImageInfo());

		if (_textures[binding] != nullptr)
			delete(_textures[binding]);

		_textures[binding] = texture;
	}

	void Core::Material::UpdateDescriptorSets()
	{
		uint32_t size = static_cast<uint32_t>(_uniformBuffers.size() + _textureBuffers.size());

		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			vector<VkWriteDescriptorSet> descriptorWrites(size);
			
			for (uint32_t j = 0; j < size; j++)
			{
				VkWriteDescriptorSet writeDescriptorSet{};

				if (_uniformBuffers.find(j) != _uniformBuffers.end())
				{
					writeDescriptorSet =
						_uniformBuffers[j]->CreateWriteDescriptorSet(i);
				}
				else if (_textureBuffers.find(j) != _textureBuffers.end())
				{
					writeDescriptorSet =
						_textureBuffers[j]->CreateWriteDescriptorSet(i);
				}

				descriptorWrites[j] = writeDescriptorSet;
				descriptorWrites[j].dstSet = _descriptorSets[i];
			}

			vkUpdateDescriptorSets(
				_device.GetDevice(),
				static_cast<uint32_t>(descriptorWrites.size()),
				descriptorWrites.data(), 0, nullptr);
		}
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
}