#include "stdafx.h"
#include "Material.h"
#include "Texture.h"
#include "SampleShader.h"
#include "UniformBuffer.h"
#include "TextureBuffer.h"
#include "PerspectiveCamera.h"
#include "CommandPool.h"
namespace Core
{
	Material::Material(Device& device)
		:_device(device)
	{
		_shader = new SampleShader(device);

		_texture = new Texture(device,
			"Textures/viking_room.png", Core::TextureFormat::Rgb_alpha, 1);
		
		//hack : binding duplication declaration.
		auto cameraBuffer = new UniformBuffer(device, sizeof(VPBufferObject), 0);
		_uniformBuffers.insert({ 0, cameraBuffer });

		//hack : binding duplication declaration.
		auto textureBuffer = new TextureBuffer(1);
		_textureBuffers.insert({ 1, textureBuffer });

		SetBuffer(1, _texture->GetDescriptorImageInfo());
		UpdateDescriptorSets();
	}

	Core::Material::~Material()
	{
		delete(_texture);

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

		delete(_shader);
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

	void Core::Material::SetBuffer(uint32_t binding, VkDescriptorImageInfo& info)
	{
		_textureBuffers[binding]->SetDescriptorImageInfo(info);
	}

	void Core::Material::UpdateDescriptorSets()
	{
		uint32_t size = static_cast<uint32_t>(_uniformBuffers.size() + _textureBuffers.size());

		for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			VkDescriptorSet* descriptorSet =
				_shader->GetDescriptorSet(i);

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
				descriptorWrites[j].dstSet = *descriptorSet;
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
}