#pragma once

namespace Core
{
	class Pipeline
	{
	public:
		Pipeline(VkRenderPass renderPass, VkDescriptorSetLayout descriptorSetLayout,
			const string& vertFilePath, const string& fragFilePath);
		~Pipeline();

		VkPipeline GetGraphicsPipeline() { return _graphicsPipeline; }
		VkPipelineLayout GetPipelineLayout() { return _pipelineLayout; }
	private:
		static vector<char> ReadFile(const string& filePath);

		void CreateGraphicsPipeline(
			VkDevice& device, VkRenderPass& renderPass, 
			VkDescriptorSetLayout& descriptorSetLayout,
			const string& vertFilePath, const string& fragFilePath);
		VkShaderModule CreateShaderModule(VkDevice& device, const vector<char>& code);
	private:
		VkPipelineLayout _pipelineLayout;
		VkPipeline _graphicsPipeline;
	};
}

