#include "stdafx.h"
#include "SampleShader.h"
#include "TextureBuffer.h"
#include "UniformBuffer.h"
#include "Vertex.h"
#include "InstanceData.h"
#include "BufferObjects/BufferObjects.h"

namespace Core
{
	SampleShader::SampleShader(Device& device)
		:Shader(device,
			"shaders/simple_vs.vert.spv",
			"shaders/simple_fs.frag.spv")
	{
		AddUniformBufferLayoutBinding(0, VK_SHADER_STAGE_VERTEX_BIT, sizeof(VPBufferObject));
		AddTextureBufferLayoutBinding(1, VK_SHADER_STAGE_FRAGMENT_BIT);
		AddTextureBufferLayoutBinding(2, VK_SHADER_STAGE_FRAGMENT_BIT);
		AddUniformBufferLayoutBinding(3, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(ShadowUniform));
	}

	type_index SampleShader::GetType()
	{
		return typeid(SampleShader);
	}

	string SampleShader::GetPass()
	{
		return "Geometry";
	}

	vector<string> SampleShader::GetVertexAttirbuteNames() const
	{
		vector<string> names = { VertexAttributeName::Position, VertexAttributeName::Col, VertexAttributeName::UV };
		return names;
	}

	void SampleShader::Prepare()
	{
		_vertexBindings = {
			Vertex::GetBindingDescription(
		0, sizeof(Vertex::Pos), VK_VERTEX_INPUT_RATE_VERTEX),
			Vertex::GetBindingDescription(
		1, sizeof(Vertex::Color), VK_VERTEX_INPUT_RATE_VERTEX),
			Vertex::GetBindingDescription(
		2, sizeof(Vertex::TexCoord), VK_VERTEX_INPUT_RATE_VERTEX),
		};

		_vertexAttributes.push_back(Vertex::GetAttributeDescription(
			0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0));
		_vertexAttributes.push_back(Vertex::GetAttributeDescription(
			1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0));
		_vertexAttributes.push_back(Vertex::GetAttributeDescription(
			2, 2, VK_FORMAT_R32G32_SFLOAT, 0));

		VkPushConstantRange pushConstant{};
		pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushConstant.size = sizeof(mat4);
		_pushConstantRanges.push_back(pushConstant);
	}

	bool SampleShader::UseInstancing()
	{
		return true;
	}
}
