#pragma once
#include "PushConstant.h"

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
	protected:
		void CreatePipelineLayout(
			vector<IDescriptor*> descriptors, vector<PushConstant> pushConstants);
	private:
		void CreateDescriptorPool(vector<IDescriptor*> descriptors);
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
	};
}