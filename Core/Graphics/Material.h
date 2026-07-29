#pragma once
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/BindlessTextureManager.h"
#include "Graphics/ResourceHandle.h"

namespace Core
{
	class Shader;
	class Texture;

	enum class AlphaMode
	{
		Opaque,
		Mask,
		Blend
	};

	class Material
	{
	public:
		Material(Device& device, Handle<Shader> shader, string materialName);
		Material(const Material& other);

		Material& operator=(const Material& other);

		virtual ~Material();

		const string GetName() const { return _name; }

		Handle<Shader> GetShaderHandle() const { return _shader; }

		template <typename T>
		const T* GetBufferConst(uint32_t binding) const
		{
			auto setIt = _buffers.find(binding);
			if (setIt == _buffers.end())
				return nullptr;

			return static_cast<const T*>(setIt->second);
		}

		Handle<Texture> GetTexture(uint32_t binding);

		void AddBuffer(uint32_t binding, void* data)
		{
			_buffers[binding] = data;
		}

		// True once bound buffers have been added. The loader uses this to skip
		// re-configuring a material it already built, now that the pool owns it for
		// the app's lifetime.
		bool HasBuffers() const { return !_buffers.empty(); }

		void AddTexture(uint32_t binding, Handle<Texture> texture)
		{
			_textures[binding] = texture;
		}

		const unordered_map<uint32_t, Handle<Texture>>& GetTexturesMap() const { return _textures; }

		// GPU Driven Rendering material
		void SetMaterialIndex(uint32_t index) { _materialIndex = index; }
		uint32_t GetMaterialIndex() const { return _materialIndex; }
		bool HasMaterialIndex() const { return _materialIndex != UINT32_MAX; }

		void SetShader(Handle<Shader> shader) { _shader = shader; }
	private:
		void SetDefault(Handle<Texture> defaultTexture);
	protected:
		Device& _device;
		Handle<Shader> _shader;

	private:
		string _name;

		bool _isDoubledSided;
		AlphaMode _alphaMode = AlphaMode::Opaque;
		bool _isAlphaCutoff;

		// Legacy bound resources (Set 1)
		unordered_map<uint32_t, void*> _buffers;
		unordered_map<uint32_t, Handle<Texture>> _textures;

		// GPU Driven Rendering material index
		uint32_t _materialIndex = UINT32_MAX;
	};
}

