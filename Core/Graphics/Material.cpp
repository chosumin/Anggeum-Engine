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
		:_device(device), _name(materialName)
	{
		_shader = device.GetResourceCache().RequestShader(shaderName);

		//HACK : In case of empty textures. This should be replaced with the shader variants system later.
		SetDefault(device.GetResourceCache().RequestDefaultTexture());
	}

	Material::Material(Device& device, string materialName, string vertPath, string fragPath)
		:_device(device), _name(materialName)
	{
		_shader = device.GetResourceCache().RequestShader(vertPath, fragPath);

		//HACK : In case of empty textures. This should be replaced with the shader variants system later.
		SetDefault(device.GetResourceCache().RequestDefaultTexture());
	}

	Material::Material(const Material& other)
		: _device(other._device),
		_shader(other._shader),
		_pushConstants(other._pushConstants),
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
		_buffers.clear();
		_textures.clear();
	}

	Shader& Core::Material::GetShader() const
	{
		return *_shader;
	}

	shared_ptr<Texture> Material::GetTexture(uint32_t binding)
	{
		auto setIt = _textures.find(binding);
		if (setIt != _textures.end())
		{
			return setIt->second;
		}
		return nullptr;
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
			if (setIndex != 1)
				continue;

			// Get texture bindings from the shader
			auto& textureBindings = descriptorLayout->GetTextureBufferBindings();
			for (auto& binding : textureBindings)
			{
				// Set default texture for each texture binding
				_textures[binding.Binding] = defaultTexture;
			}
		}
	}
}