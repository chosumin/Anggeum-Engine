#pragma once

namespace Core
{
	class RenderPass;
	class Shader;
	class PipelineState;
	class Pipeline
	{
	public:
		Pipeline(Device& device, RenderPass& renderPass, Shader& shader, PipelineState& pipelineState);
		~Pipeline();

		VkPipeline GetGraphicsPipeline() const { return _graphicsPipeline; }
		VkPipelineBindPoint GetPipelineBindPoint() const { return _pipelineBindPoint; }
	private:
		VkPipelineInputAssemblyStateCreateInfo GetInputAssemblyStateCreateInfo();
		VkPipelineViewportStateCreateInfo GetViewportStateCreateInfo();
		VkPipelineDepthStencilStateCreateInfo GetDepthStencilStateCreateInfo();
		VkPipelineColorBlendStateCreateInfo GetColorBlendStateCreateInfo(VkPipelineColorBlendAttachmentState& colorBlendAttachment);
		VkPipelineDynamicStateCreateInfo GetDynamicStateCreateInfo();
		VkPipelineColorBlendAttachmentState GetColorBlendAttachmentState();
	private:
		Device& _device;

		VkPipeline _graphicsPipeline;
		VkPipelineBindPoint _pipelineBindPoint;

		vector<VkDynamicState> _dynamicStates =
		{
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR,
		};
	};
}

