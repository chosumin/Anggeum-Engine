#pragma once
#include "PushConstant.h"
#include "UniformBuffer.h"
#include "TextureBuffer.h"

namespace Core
{
	class IDescriptor;
	class DescriptorPool;
	class Shader
	{
	public:
		Shader(Device& device, 
			const string& vertFilePath,
			const string& fragFilePath);
		virtual ~Shader();

		virtual std::type_index GetType() = 0;
		virtual string GetPass() = 0;
		virtual bool UseInstancing() = 0;

		virtual VkPipelineVertexInputStateCreateInfo GetVertexInputStateCreateInfo();
		
		virtual vector<string> GetVertexAttirbuteNames() const = 0;

		vector<VkPipelineShaderStageCreateInfo> GetShaderStageCreateInfo() const;

		VkShaderStageFlags GetPushConstantsShaderStage() const;
		vector<VkPushConstantRange>& GetPushConstantRanges() { return _pushConstantRanges; }
		VkDescriptorSetLayout& GetDescriptorSetLayout();
		VkDescriptorPool& GetDescriptorPool();
		VkPipelineLayout GetPipelineLayout() { return _pipelineLayout; }

		const vector<UniformBufferLayoutBinding>& GetUniformBufferLayoutBindings() const
		{
			return _uniformBufferLayoutBindings;
		}

		const vector<TextureBufferLayoutBinding>& GetTextureBufferLayoutBindings() const
		{
			return _textureBufferLayoutBindings;
		}
	protected:
		void CreatePipelineLayout(vector<PushConstant> pushConstants);

		void AddUniformBufferLayoutBinding(uint32_t binding, VkShaderStageFlagBits stage, VkDeviceSize size);
		void AddTextureBufferLayoutBinding(uint32_t binding, VkShaderStageFlagBits stage);
	private:
		void CreateDescriptorPool();
		void CreatePushConstants(vector<PushConstant>& pushConstants);

		vector<char> ReadFile(const string& filePath);
		VkShaderModule CreateShaderModule(VkDevice& device, const vector<char>& code) const;
	protected:
		vector<VkPushConstantRange> _pushConstantRanges;

		vector<VkVertexInputBindingDescription> _vertexBindings;
		vector<VkVertexInputAttributeDescription> _vertexAttributes;
	private:
		Device& _device;

		VkShaderModule _vertShaderModule;
		VkShaderModule _fragShaderModule;

		DescriptorPool* _descriptorPool;

		VkPipelineLayout _pipelineLayout;

		vector<UniformBufferLayoutBinding> _uniformBufferLayoutBindings;
		vector<TextureBufferLayoutBinding> _textureBufferLayoutBindings;
	};
}