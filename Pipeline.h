#pragma once

namespace Core
{
	class IDescriptor;
	class RenderPass;
	class IPushConstant;
	class Shader;
	class Pipeline
	{
	public:
		Pipeline(Device& device, const RenderPass& renderPass, const Shader& shader);
		~Pipeline();

		VkPipeline GetGraphicsPipeline() { return _graphicsPipeline; }
		//VkPipelineLayout GetPipelineLayout() { return _pipelineLayout; }
		//VkDescriptorSet* GetDescriptorSet(size_t index) { return &_descriptorSets[index]; }
	private:
		//static vector<char> ReadFile(const string& filePath);

		/*void CreateGraphicsPipeline(
			VkDevice& device, VkRenderPass renderPass, VkSampleCountFlagBits msaaSamples,
			const string& vertFilePath, const string& fragFilePath);*/
		//VkShaderModule CreateShaderModule(VkDevice& device, const vector<char>& code) const;

		//void CreateDescriptors(vector<IDescriptor*> descriptors);
		//void CreateDescriptorSetLayout(vector<IDescriptor*> descriptors);
		//void CreateDescriptorPool(vector<IDescriptor*> descriptors);
		//void CreateDescriptorSets(vector<IDescriptor*> descriptors);

		//void CreatePushConstants(vector<IPushConstant*> pushConstants);

		VkPipelineInputAssemblyStateCreateInfo GetInputAssemblyStateCreateInfo();
		VkPipelineViewportStateCreateInfo GetViewportStateCreateInfo();
		VkPipelineRasterizationStateCreateInfo GetRasterizationStateCreateInfo();
		VkPipelineMultisampleStateCreateInfo GetMultisampleStateCreateInfo(VkSampleCountFlagBits sampleCount);
		VkPipelineDepthStencilStateCreateInfo GetDepthStencilStateCreateInfo();
		VkPipelineColorBlendStateCreateInfo GetColorBlendStateCreateInfo();
		VkPipelineDynamicStateCreateInfo GetDynamicStateCreateInfo();
	private:
		Device& _device;
		VkPipeline _graphicsPipeline;
		//VkPipelineLayout _pipelineLayout;
		//VkDescriptorSetLayout _descriptorSetLayout;
		//VkDescriptorPool _descriptorPool;
		//vector<VkDescriptorSet> _descriptorSets;
		//vector<VkPushConstantRange> _pushConstants;
	};
}

