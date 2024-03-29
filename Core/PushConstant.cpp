#include "stdafx.h"
#include "PushConstant.h"

Core::PushConstant::PushConstant(uint32_t size, VkShaderStageFlags stageFlags)
	:Size(size), StageFlags(stageFlags)
{
}
