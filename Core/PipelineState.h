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
	private:
		void InitRasterizationStateCreateInfo();
		void InitMultisampleStateCreateInfo();
	private:
		VkPipelineRasterizationStateCreateInfo _rasterizationStateCreateInfo{};
		VkPipelineMultisampleStateCreateInfo _multisampleStateCreateInfo{};
	};
}