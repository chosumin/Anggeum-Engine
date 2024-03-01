#pragma once

namespace Core
{
	struct PushConstant
	{
	public:
		PushConstant(uint32_t size, VkShaderStageFlags stageFlags);
	public:
		uint32_t Size;
		VkShaderStageFlags StageFlags;
	};
}
