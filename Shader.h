#pragma once
#include "PushConstant.h"

namespace Core
{
	class IDescriptor;
	class Shader
	{
	public:
		Shader(Device& device, 
			const string& vertFilePath,
			const string& fragFilePath);
		virtual ~Shader();

		virtual std::type_index GetType() = 0;
		virtual string GetPass() = 0;

		vector<VkPipelineShaderStageCreateInfo> GetShaderStageCreateInfo() const;
		VkPipelineVertexInputStateCreateInfo GetVertexInputStateCreateInfo();

		VkDescriptorSet* GetDescriptorSet(size_t index) { return &_descriptorSets[index]; }

		VkShaderStageFlags GetPushConstantsShaderStage() const;
		vector<VkPushConstantRange>& GetPushConstantRanges() { return _pushConstantRanges; }
		VkDescriptorSetLayout& GetDescriptorSetLayout() { return _descriptorSetLayout; }
		VkPipelineLayout GetPipelineLayout() { return _pipelineLayout; }
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
		vector<VkPushConstantRange> _pushConstantRanges;
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