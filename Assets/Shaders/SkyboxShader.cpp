#include "stdafx.h"
#include "SkyboxShader.h"

SkyboxShader::SkyboxShader(Core::Device& device)
	:Shader(device,
		"shaders/skybox.vert.spv",
		"shaders/skybox.frag.spv")
{
	AddUniformBufferLayoutBinding(0, VK_SHADER_STAGE_VERTEX_BIT, sizeof(VPBufferObject));
	AddTextureBufferLayoutBinding(1, VK_SHADER_STAGE_FRAGMENT_BIT);
}

type_index SkyboxShader::GetType()
{
	return typeid(SkyboxShader);
}

string SkyboxShader::GetPass()
{
	return "Skybox";
}

bool SkyboxShader::UseInstancing()
{
	return false;
}

vector<string> SkyboxShader::GetVertexAttirbuteNames() const
{
	vector<string> names = { VertexAttributeName::Position };
	return names;
}

void SkyboxShader::Prepare()
{
	_vertexBindings = {
			Vertex::GetBindingDescription(
		0, 12, VK_VERTEX_INPUT_RATE_VERTEX),
	};

	_vertexAttributes.push_back(Vertex::GetAttributeDescription(
		0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0));

	VkPushConstantRange pushConstant{};
	pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	pushConstant.size = sizeof(mat4);
	_pushConstantRanges.push_back(pushConstant);
}
