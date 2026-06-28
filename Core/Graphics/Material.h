#pragma once
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/BindlessTextureManager.h"

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
		Material(Device& device, string shaderName, string materialName);
		Material(Device& device, string materialName, string vertPath, string fragPath);
		Material(const Material& other); 

		Material& operator=(const Material& other);

		virtual ~Material();

		const string GetName() const { return _name; }

		Shader& GetShader() const;
		weak_ptr<Shader> GetShaderPtr() const { return _shader; }

		void* GetBuffer(uint32_t binding)
		{
			auto setIt = _buffers.find(binding);
			if (setIt == _buffers.end())
				return nullptr;

			return setIt->second;
		}

		template <typename T>
		const T* GetBufferConst(uint32_t binding) const
		{
			auto setIt = _buffers.find(binding);
			if (setIt == _buffers.end())
				return nullptr;

			return static_cast<const T*>(setIt->second);
		}

		shared_ptr<Texture> GetTexture(uint32_t binding);

		void AddBuffer(uint32_t binding, void* data)
		{
			_buffers[binding] = data;
		}

		void AddTexture(uint32_t binding, shared_ptr<Texture> texture)
		{
			_textures[binding] = texture;
		}

		// Bindless texture methods (no binding index needed)
		void AddBindlessTexture(TextureHandle handle)
		{
			_bindlessTextureHandles.push_back(handle);
		}

		void SetBindlessTexture(size_t index, TextureHandle handle)
		{
			if (index >= _bindlessTextureHandles.size())
			{
				_bindlessTextureHandles.resize(index + 1);
			}
			_bindlessTextureHandles[index] = handle;
		}

		TextureHandle GetBindlessTexture(size_t index) const
		{
			if (index >= _bindlessTextureHandles.size())
				return TextureHandle{};
			return _bindlessTextureHandles[index];
		}

		const vector<TextureHandle>& GetBindlessTexturesVector() const
		{
			return _bindlessTextureHandles;
		}

		size_t GetBindlessTextureCount() const
		{
			return _bindlessTextureHandles.size();
		}

		void ClearBindlessTextures()
		{
			_bindlessTextureHandles.clear();
		}

		template <typename T>
		inline void SetPushConstants(const T& value)
		{
			vector<uint8_t> converted =
				vector<uint8_t>{ reinterpret_cast<const uint8_t*>(&value),
				reinterpret_cast<const uint8_t*>(&value) + sizeof(T) };

			_pushConstants.insert(_pushConstants.end(), converted.begin(), converted.end());
		}
		vector<uint8_t>* GetPushConstantsData();
		void ClearPushConstantsCache();

		const unordered_map<uint32_t, void*>& GetBuffersMap() const { return _buffers; }
		const unordered_map<uint32_t, shared_ptr<Texture>>& GetTexturesMap() const { return _textures; }

		// GPU Driven Rendering material
		void SetMaterialIndex(uint32_t index) { _materialIndex = index; }
		uint32_t GetMaterialIndex() const { return _materialIndex; }
		bool HasMaterialIndex() const { return _materialIndex != UINT32_MAX; }

		void SetShader(shared_ptr<Shader> shader) { _shader = shader; }
	private:
		void SetDefault(shared_ptr<Texture> defaultTexture);
	protected:
		Device& _device;
		shared_ptr<Shader> _shader;
		vector<uint8_t> _pushConstants;

	private:
		string _name;

		bool _isDoubledSided;
		AlphaMode _alphaMode = AlphaMode::Opaque;
		bool _isAlphaCutoff;

		// Legacy bound resources (Set 1)
		unordered_map<uint32_t, void*> _buffers;
		unordered_map<uint32_t, shared_ptr<Texture>> _textures;

		// Bindless texture handles (Set 2, Binding 0)
		// Just store handles in order, no binding index needed
		vector<TextureHandle> _bindlessTextureHandles;

		// GPU Driven Rendering material index
		uint32_t _materialIndex = UINT32_MAX;
	};
}

