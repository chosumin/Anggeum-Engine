#pragma once
#include "UniformBuffer.h"
#include "TextureBuffer.h"
#include "Texture.h"

namespace Core
{
	class Device;
	class Shader;
	class Texture;
	class UniformBuffer;
	class TextureBuffer;
	class CommandPool;
	class Material
	{
	public:
		Material(Device& device);
		Material(const Material& other) = default;
		virtual ~Material();

		virtual type_index GetType() const = 0;

		Shader& GetShader() const;
		void SetShader(Shader& shader);

		void SetTexture(uint32_t binding, Texture& texture);
		void SetBuffer(uint32_t currentImage, uint32_t binding, void* data);
		void SetBuffer(uint32_t binding, Texture* texture);

		void UpdateDescriptorSets();

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
	protected:
		Device& _device;
		Shader* _shader;
		unordered_map<uint32_t, Texture*> _textures;
		vector<uint8_t> _pushConstants;
		unordered_map<uint32_t, UniformBuffer*> _uniformBuffers;
		unordered_map<uint32_t, TextureBuffer*> _textureBuffers;
	};
}

