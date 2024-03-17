#pragma once

namespace Core
{
	class IDescriptor;
	class RenderPass;
	class Shader;
	class Pipeline
	{
	public:
		Pipeline(Device& device, const RenderPass& renderPass, Shader& shader);
		~Pipeline();

		VkPipeline GetGraphicsPipeline() { return _graphicsPipeline; }
	private:
		VkPipelineInputAssemblyStateCreateInfo GetInputAssemblyStateCreateInfo();
		VkPipelineViewportStateCreateInfo GetViewportStateCreateInfo();
		VkPipelineRasterizationStateCreateInfo GetRasterizationStateCreateInfo();
		VkPipelineMultisampleStateCreateInfo GetMultisampleStateCreateInfo(VkSampleCountFlagBits sampleCount);
		VkPipelineDepthStencilStateCreateInfo GetDepthStencilStateCreateInfo();
		VkPipelineColorBlendStateCreateInfo GetColorBlendStateCreateInfo(VkPipelineColorBlendAttachmentState& colorBlendAttachment);
		VkPipelineDynamicStateCreateInfo GetDynamicStateCreateInfo();
		VkPipelineColorBlendAttachmentState GetColorBlendAttachmentState();
	private:
		Device& _device;

		VkPipeline _graphicsPipeline;

		vector<VkDynamicState> _dynamicStates =
		{
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};
	};
}

