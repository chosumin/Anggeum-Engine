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
	private:
		void InitRasterizationStateCreateInfo();
		void InitMultisampleStateCreateInfo();
		void InitDepthStencilStateCreateInfo();
	private:
		VkPipelineRasterizationStateCreateInfo _rasterizationStateCreateInfo{};
		VkPipelineMultisampleStateCreateInfo _multisampleStateCreateInfo{};
		VkPipelineDepthStencilStateCreateInfo _depthStencilStateCreateInfo{};
	};
}