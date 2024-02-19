#include "stdafx.h"
#include "IPushConstant.h"

Core::ModelPushConstant::ModelPushConstant()
	:Matrix(mat4(1)), _offset(0)
{
}

uint32_t Core::ModelPushConstant::GetSize() const
{
	return sizeof(mat4);
}

VkPushConstantRange Core::ModelPushConstant::GetPushConstantRange() const
{
	VkPushConstantRange pushConstant;
	pushConstant.offset = _offset;
	pushConstant.size = GetSize();
	pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	return pushConstant;
}

uint32_t Core::ModelPushConstant::GetOffset() const
{
	return _offset;
}

void Core::ModelPushConstant::SetOffset(uint32_t offset)
{
	_offset = offset;
}
