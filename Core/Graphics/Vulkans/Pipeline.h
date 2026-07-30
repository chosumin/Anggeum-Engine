#pragma once

namespace Core
{
	class RenderPass;
	class Shader;
	class PipelineState;

	// Attachment formats for a dynamic-rendering pipeline; replaces the
	// VkRenderPass a classic pipeline is compiled against.
	struct PipelineRenderingDesc
	{
		vector<VkFormat> colorFormats;
		VkFormat depthFormat = VK_FORMAT_UNDEFINED;
		VkFormat stencilFormat = VK_FORMAT_UNDEFINED;
	};

	class Pipeline
	{
	public:
		Pipeline(Device& device, RenderPass& renderPass, Shader& shader, PipelineState& pipelineState);
		Pipeline(Device& device, const PipelineRenderingDesc& renderingDesc, Shader& shader, PipelineState& pipelineState);
		Pipeline(Device& device, Shader& shader);
		~Pipeline();

		VkPipeline GetPipeline() const { return _pipeline; }
		VkPipelineBindPoint GetPipelineBindPoint() const { return _pipelineBindPoint; }
	private:
		void CreateGraphicsPipeline(VkRenderPass renderPass,
			const VkPipelineRenderingCreateInfo* renderingInfo,
			Shader& shader, PipelineState& pipelineState);
		VkPipelineViewportStateCreateInfo GetViewportStateCreateInfo();
		// One entry per color attachment; the vector must outlive pipeline creation.
		VkPipelineColorBlendStateCreateInfo GetColorBlendStateCreateInfo(
			const vector<VkPipelineColorBlendAttachmentState>& blendAttachments);
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

