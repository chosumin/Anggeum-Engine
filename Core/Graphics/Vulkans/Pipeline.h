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
		Pipeline(Device& device, Shader& shader);
		~Pipeline();

		VkPipeline GetPipeline() const { return _pipeline; }
		VkPipelineBindPoint GetPipelineBindPoint() const { return _pipelineBindPoint; }
	private:
		void CreateGraphicsPipeline(RenderPass& renderPass, Shader& shader, PipelineState& pipelineState);
		VkPipelineViewportStateCreateInfo GetViewportStateCreateInfo();
		VkPipelineColorBlendStateCreateInfo GetColorBlendStateCreateInfo(VkPipelineColorBlendAttachmentState& colorBlendAttachment);
		VkPipelineDynamicStateCreateInfo GetDynamicStateCreateInfo();
		VkPipelineColorBlendAttachmentState GetColorBlendAttachmentState();
	private:
		Device& _device;

		VkPipeline _pipeline;
		VkPipelineBindPoint _pipelineBindPoint;

		vector<VkDynamicState> _dynamicStates =
		{
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR,
			VK_DYNAMIC_STATE_DEPTH_BIAS,
		};
	};
}

