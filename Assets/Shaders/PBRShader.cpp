#include "stdafx.h"
#include "PBRShader.h"

namespace Core
{
	PBRShader::PBRShader(Device& device)
		:Shader(device,
			"shaders/pbr.vert.spv",
			"shaders/pbr.frag.spv")
	{
		AddUniformBufferLayoutBinding(0, static_cast<VkShaderStageFlagBits>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT), sizeof(VPBufferObject));
		AddTextureBufferLayoutBinding(1, VK_SHADER_STAGE_FRAGMENT_BIT);
		AddTextureBufferLayoutBinding(2, VK_SHADER_STAGE_FRAGMENT_BIT);
		AddTextureBufferLayoutBinding(3, VK_SHADER_STAGE_FRAGMENT_BIT);
		AddTextureBufferLayoutBinding(4, VK_SHADER_STAGE_FRAGMENT_BIT);
		AddUniformBufferLayoutBinding(5, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(ShadowUniform));
		AddUniformBufferLayoutBinding(6, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(PBRBuffer));
		AddUniformBufferLayoutBinding(7, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(LightBuffer));
		AddTextureBufferLayoutBinding(8, VK_SHADER_STAGE_FRAGMENT_BIT);
		AddTextureBufferLayoutBinding(9, VK_SHADER_STAGE_FRAGMENT_BIT);
		AddTextureBufferLayoutBinding(10, VK_SHADER_STAGE_FRAGMENT_BIT);
	}

	type_index PBRShader::GetType()
	{
		return typeid(PBRShader);
	}

	string PBRShader::GetPass()
	{
		return "Geometry";
	}

	vector<string> PBRShader::GetVertexAttirbuteNames() const
	{
		vector<string> names = { 
			VertexAttributeName::Position, 
			VertexAttributeName::Col, 
			VertexAttributeName::Normal,
			VertexAttributeName::UV };
		return names;
	}

	void PBRShader::Prepare()
	{
		_vertexBindings = {
			Vertex::GetBindingDescription(
				0, 12, VK_VERTEX_INPUT_RATE_VERTEX),
			Vertex::GetBindingDescription(
				1, 12, VK_VERTEX_INPUT_RATE_VERTEX),
			Vertex::GetBindingDescription(
				2, 12, VK_VERTEX_INPUT_RATE_VERTEX),
			Vertex::GetBindingDescription(
				3, 8, VK_VERTEX_INPUT_RATE_VERTEX),
		};

		_vertexAttributes.push_back(Vertex::GetAttributeDescription(
			0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0));
		_vertexAttributes.push_back(Vertex::GetAttributeDescription(
			1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0));
		_vertexAttributes.push_back(Vertex::GetAttributeDescription(
			2, 2, VK_FORMAT_R32G32B32_SFLOAT, 0));
		_vertexAttributes.push_back(Vertex::GetAttributeDescription(
			3, 3, VK_FORMAT_R32G32_SFLOAT, 0));

		VkPushConstantRange pushConstant{};
		pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushConstant.size = sizeof(mat4);
		_pushConstantRanges.push_back(pushConstant);
	}

	bool PBRShader::UseInstancing()
	{
		return true;
	}
}
