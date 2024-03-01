#pragma once
#include "PushConstant.h"

namespace Core
{
	class UniformBuffer;
	class TextureBuffer;
	class IDescriptor;
	class Shader
	{
	public:
		Shader(Device& device, 
			const string& vertFilePath,
			const string& fragFilePath);
		virtual ~Shader();

		virtual std::type_index GetType() = 0;

		vector<VkPipelineShaderStageCreateInfo> GetShaderStageCreateInfo() const;
		VkPipelineVertexInputStateCreateInfo GetVertexInputStateCreateInfo();
		VkPipelineLayout GetPipelineLayout() const
		{
			return _pipelineLayout;
		}

		VkDescriptorSet* GetDescriptorSet(size_t index) { return &_descriptorSets[index]; }

		VkShaderStageFlags GetPushConstantsShaderStage() const;
		vector<uint8_t>* GetPushConstantsData();
		void ClearPushConstantsCache();

		void SetBuffer(uint32_t currentImage, uint32_t binding, void* data);
		void SetBuffer(uint32_t binding, VkDescriptorImageInfo& info);

		template <typename T>
		inline void SetPushConstants(const T& value)
		{
			vector<uint8_t> converted = 
				vector<uint8_t>{reinterpret_cast<const uint8_t*>(&value),
				reinterpret_cast<const uint8_t*>(&value) + sizeof(T)};

			_pushConstants.insert(_pushConstants.end(), converted.begin(), converted.end());
		}

		void UpdateDescriptorSets();
	protected:
		void CreatePipelineLayout(
			vector<IDescriptor*> descriptors, vector<PushConstant> pushConstants);
	private:
		void CreateDescriptors(vector<IDescriptor*> descriptors);
		void CreateDescriptorSetLayout(vector<IDescriptor*> descriptors);
		void CreateDescriptorPool(vector<IDescriptor*> descriptors);
		void CreateDescriptorSets(vector<IDescriptor*> descriptors);
		void CreatePushConstants(vector<PushConstant>& pushConstants);

		vector<char> ReadFile(const string& filePath);
		VkShaderModule CreateShaderModule(VkDevice& device, const vector<char>& code) const;
	protected:
		unordered_map<uint32_t, UniformBuffer*> _uniformBuffers;
		unordered_map<uint32_t, TextureBuffer*> _textureBuffers;
		vector<VkPushConstantRange> _pushConstantRanges;
		vector<uint8_t> _pushConstants;
	private:
		Device& _device;

		VkShaderModule _vertShaderModule;
		VkShaderModule _fragShaderModule;

		vector<VkDescriptorSet> _descriptorSets;
		VkDescriptorPool _descriptorPool;
		VkDescriptorSetLayout _descriptorSetLayout;
		VkPipelineLayout _pipelineLayout;

		VkVertexInputBindingDescription _vertexBinding;
		vector<VkVertexInputAttributeDescription> _vertexAttribute;
	};
}