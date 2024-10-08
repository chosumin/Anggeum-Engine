#pragma once
#include "VulkanWrapper/UniformBuffer.h"
#include "VulkanWrapper/TextureBuffer.h"
#include "VulkanWrapper/Texture.h"

namespace Core
{
	class Device;
	class Shader;
	class CommandPool;
	struct RenderTarget;
	class Material
	{
	public:
		Material(Device& device, string shaderName, uint32_t hash);
		Material(const Material& other) = default;
		virtual ~Material();

		const uint32_t GetHash() const { return _hash; }

		Shader& GetShader() const;
		void SetShader(Shader& shader);

		void SetBuffer(uint32_t currentImage, uint32_t binding, void* data);
		void SetBuffer(uint32_t binding, Texture* texture);
		void SetBuffer(uint32_t binding, RenderTarget* renderTarget);

		const VkDescriptorSet& GetDescriptorSet(size_t index) const
		{
			return _descriptorSets[index];
		}

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

		bool IsDirty() { return _isDirty; }
	private:
		void CreateDescriptorSets();
		void CreateBuffers();
	protected:
		Device& _device;
		Shader* _shader;
		unordered_map<uint32_t, Texture*> _textures;
		vector<uint8_t> _pushConstants;
		unordered_map<uint32_t, UniformBuffer*> _uniformBuffers;
		unordered_map<uint32_t, TextureBuffer*> _textureBuffers;
	private:
		bool _isDirty;

		uint32_t _hash;

		vector<VkDescriptorSet> _descriptorSets;
	};
}

