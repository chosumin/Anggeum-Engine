#include "stdafx.h"
#include "ShadowShader.h"
#include "UniformBuffer.h"
#include "InstanceData.h"
#include "Vertex.h"
#include "BufferObjects/BufferObjects.h"

ShadowShader::ShadowShader(Core::Device& device)
	:Shader(device,
		"shaders/shadow.vert.spv",
		"shaders/shadow.frag.spv")
{
	AddUniformBufferLayoutBinding(0, VK_SHADER_STAGE_VERTEX_BIT, sizeof(VPBufferObject));

	CreatePipelineLayout({});
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

VkPipelineVertexInputStateCreateInfo ShadowShader::GetVertexInputStateCreateInfo()
{
	_vertexBindings = {
			Vertex::GetBindingDescription(
		0, sizeof(ShadowVertex), VK_VERTEX_INPUT_RATE_VERTEX),
			Vertex::GetBindingDescription(
		1, sizeof(Core::InstanceData), VK_VERTEX_INPUT_RATE_INSTANCE)
	};

	_vertexAttributes.push_back(Vertex::GetAttributeDescription(
		0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0));
	_vertexAttributes.push_back(Vertex::GetAttributeDescription(
		1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0));
	_vertexAttributes.push_back(Vertex::GetAttributeDescription(
		1, 2, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 4));
	_vertexAttributes.push_back(Vertex::GetAttributeDescription(
		1, 3, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 8));
	_vertexAttributes.push_back(Vertex::GetAttributeDescription(
		1, 4, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 12));

	return Shader::GetVertexInputStateCreateInfo();
}

vector<string> ShadowShader::GetVertexAttirbuteNames() const
{
	vector<string> names = { VertexAttributeName::Position };
	return names;
}
