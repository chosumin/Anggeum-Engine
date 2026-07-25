#include "stdafx.h"
#include "Material.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/DescriptorPool.h"
#include "Graphics/ResourceCache.h"

namespace Core
{
	Material::Material(Device& device, Handle<Shader> shader, string materialName)
		:_device(device), _shader(shader), _name(materialName)
	{
		//HACK : In case of empty textures. This should be replaced with the shader variants system later.
		SetDefault(device.GetResourceCache().GetDefaultTextureHandle());
	}

	Material::Material(const Material& other)
		: _device(other._device),
		  _shader(other._shader),
		  _name(other._name + "_copy"),
		  _isDoubledSided(other._isDoubledSided),
		  _alphaMode(other._alphaMode),
		  _isAlphaCutoff(other._isAlphaCutoff),
		  _buffers(other._buffers),
		  _textures(other._textures)
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
		}
		return *this;
	}

	Core::Material::~Material()
	{
		// Buffers are now owned by RenderFrame, so we don't delete them here
		_buffers.clear();
		_textures.clear();
	}

	Handle<Texture> Material::GetTexture(uint32_t binding)
	{
		auto setIt = _textures.find(binding);
		if (setIt != _textures.end())
		{
			return setIt->second;
		}
		return Handle<Texture>{};
	}

	void Material::SetDefault(Handle<Texture> defaultTexture)
	{
		auto& layouts = _shader.Get().GetDescriptorSetLayouts();
		
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