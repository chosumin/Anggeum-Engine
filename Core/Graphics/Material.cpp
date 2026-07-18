#include "stdafx.h"
#include "Material.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/DescriptorPool.h"
#include "Graphics/ResourceCache.h"

namespace Core
{
	Material::Material(Device& device, string shaderName, string materialName)
		:_device(device), _name(materialName)
	{
		_shader = device.GetResourceCache().RequestShader(shaderName);

		//HACK : In case of empty textures. This should be replaced with the shader variants system later.
		SetDefault(device.GetResourceCache().GetDefaultTexture());
	}

	Material::Material(Device& device, string materialName, string vertPath, string fragPath)
		:_device(device), _name(materialName)
	{
		_shader = device.GetResourceCache().RequestShader(vertPath, fragPath);

		//HACK : In case of empty textures. This should be replaced with the shader variants system later.
		SetDefault(device.GetResourceCache().GetDefaultTexture());
	}

	Material::Material(const Material& other)
		: _device(other._device),
		  _shader(other._shader),
		  _name(other._name + "_copy"),
		  _isDoubledSided(other._isDoubledSided),
		  _alphaMode(other._alphaMode),
		  _isAlphaCutoff(other._isAlphaCutoff),
		  _buffers(other._buffers),
		  _textures(other._textures),
		  _bindlessTextureHandles(other._bindlessTextureHandles) // Copy bindless handles
	{
	}

	Material& Material::operator=(const Material& other)
	{
		if (this != &other)
		{
			_shader = other._shader;
			_name = other._name;
			_isDoubledSided = other._isDoubledSided;
			_alphaMode = other._alphaMode;
			_isAlphaCutoff = other._isAlphaCutoff;
			_buffers = other._buffers;
			_textures = other._textures;
			_bindlessTextureHandles = other._bindlessTextureHandles; // Copy bindless handles
		}
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

		//todo: set first texture of bindless as a default texture
	}
}