#pragma once
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/UniformBuffer.h"
#include "Graphics/Vulkans/TextureBuffer.h"
#include "Graphics/Vulkans/StorageBuffer.h"

namespace Core
{
	class Device;
	class Shader;
	class Texture;
	class RenderFrame;

	enum class AlphaMode
	{
		/// Alpha value is ignored
		Opaque,
		/// Either full opaque or fully transparent
		Mask,
		/// Output is combined with the background
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

		shared_ptr<Texture> GetTexture(uint32_t binding);

		void AddBuffer(uint32_t binding, void* data)
		{
			_buffers[binding] = data;
		}

		void AddTexture(uint32_t binding, shared_ptr<Texture> texture)
		{
			_textures[binding] = texture;
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

		unordered_map<uint32_t, void*> _buffers;
		unordered_map<uint32_t, shared_ptr<Texture>> _textures;
	};
}

