#include "stdafx.h"
#include "ShadowShader.h"
#include "VulkanWrapper/UniformBuffer.h"
#include "VulkanWrapper/Vertex.h"
#include "BufferObjects/BufferObjects.h"

ShadowShader::ShadowShader(Core::Device& device)
	:Shader(device,
		"shaders/shadow.vert.spv",
		"shaders/shadow.frag.spv")
{
	AddUniformBufferLayoutBinding(0, VK_SHADER_STAGE_VERTEX_BIT, sizeof(VPBufferObject));
}

type_index ShadowShader::GetType()
{
	return typeid(ShadowShader);
}

string ShadowShader::GetPass()
{
	return string();
}

bool ShadowShader::UseInstancing()
{
	return true;
}

vector<string> ShadowShader::GetVertexAttirbuteNames() const
{
	vector<string> names = { VertexAttributeName::Position };
	return names;
}

void ShadowShader::Prepare()
{
	_vertexBindings = {
			Vertex::GetBindingDescription(
		0, sizeof(ShadowVertex), VK_VERTEX_INPUT_RATE_VERTEX),
	};

	_vertexAttributes.push_back(Vertex::GetAttributeDescription(
		0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0));

	VkPushConstantRange pushConstant{};
	pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	pushConstant.size = sizeof(mat4);
	_pushConstantRanges.push_back(pushConstant);
}
