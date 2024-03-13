#pragma once

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
		virtual ~Material();

		Shader& GetShader() const;
		void SetShader(Shader& shader);

		void SetTexture(uint32_t binding, Texture& texture);
		void SetBuffer(uint32_t currentImage, uint32_t binding, void* data);
		void SetBuffer(uint32_t binding, VkDescriptorImageInfo& info);

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
	private:
		Device& _device;
		Shader* _shader;
		Texture* _texture;
		vector<uint8_t> _pushConstants;
		unordered_map<uint32_t, UniformBuffer*> _uniformBuffers;
		unordered_map<uint32_t, TextureBuffer*> _textureBuffers;
	};
}

