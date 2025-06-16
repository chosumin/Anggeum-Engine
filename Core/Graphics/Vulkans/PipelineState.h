#pragma once

namespace Core
{
	class PipelineState
	{
	public:
		PipelineState();

		VkPipelineRasterizationStateCreateInfo& GetRasterizationStateCreateInfo() 
		{ 
			return _rasterizationStateCreateInfo; 
		}

		VkPipelineMultisampleStateCreateInfo& GetMultisampleStateCreateInfo()
		{ 
			return _multisampleStateCreateInfo;
		}

		VkPipelineDepthStencilStateCreateInfo& GetDepthStencilStateCreateInfo()
		{
			return _depthStencilStateCreateInfo;
		}

		VkPipelineInputAssemblyStateCreateInfo& GetInputAssemblyStateCreateInfo()
		{
			return _inputAssemblyStateCreateInfo;
		}
	private:
		void InitRasterizationStateCreateInfo();
		void InitMultisampleStateCreateInfo();
		void InitDepthStencilStateCreateInfo();
		void InitInputAssemblyStateCreateInfo();
	private:
		VkPipelineInputAssemblyStateCreateInfo _inputAssemblyStateCreateInfo{};
		VkPipelineRasterizationStateCreateInfo _rasterizationStateCreateInfo{};
		VkPipelineMultisampleStateCreateInfo _multisampleStateCreateInfo{};
		VkPipelineDepthStencilStateCreateInfo _depthStencilStateCreateInfo{};
	};
}