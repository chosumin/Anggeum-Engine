#include "stdafx.h"
#include "SampleShader.h"
#include "TextureBuffer.h"
#include "UniformBuffer.h"
#include "Vertex.h"
#include "InstanceData.h"

namespace Core
{
	SampleShader::SampleShader(Device& device)
		:Shader(device,
			"shaders/simple_vs.vert.spv",
			"shaders/simple_fs.frag.spv")
	{
		auto cameraBuffer = new UniformBufferLayoutBinding(0, VK_SHADER_STAGE_VERTEX_BIT);
		auto textureBuffer = new TextureBufferLayoutBinding(1);
		auto shadowBuffer = new TextureBufferLayoutBinding(2);
		auto lightBuffer = new UniformBufferLayoutBinding(3, VK_SHADER_STAGE_FRAGMENT_BIT);

		CreatePipelineLayout({ cameraBuffer, textureBuffer, shadowBuffer, lightBuffer }, {});
	}

	type_index SampleShader::GetType()
	{
		return typeid(SampleShader);
	}

	string SampleShader::GetPass()
	{
		return "Geometry";
	}

	VkPipelineVertexInputStateCreateInfo SampleShader::GetVertexInputStateCreateInfo()
	{
		_vertexBindings = {
			Vertex::GetBindingDescription(
		0, sizeof(Vertex::Pos), VK_VERTEX_INPUT_RATE_VERTEX),
			Vertex::GetBindingDescription(
		1, sizeof(Vertex::Color), VK_VERTEX_INPUT_RATE_VERTEX),
			Vertex::GetBindingDescription(
		2, sizeof(Vertex::TexCoord), VK_VERTEX_INPUT_RATE_VERTEX),
			Vertex::GetBindingDescription(
		3, sizeof(InstanceData), VK_VERTEX_INPUT_RATE_INSTANCE)
		};

		_vertexAttributes.push_back(Vertex::GetAttributeDescription(
			0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0));
		_vertexAttributes.push_back(Vertex::GetAttributeDescription(
			1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0));
		_vertexAttributes.push_back(Vertex::GetAttributeDescription(
			2, 2, VK_FORMAT_R32G32_SFLOAT, 0));

		_vertexAttributes.push_back(Vertex::GetAttributeDescription(
			3, 3, VK_FORMAT_R32G32B32A32_SFLOAT, 0));
		_vertexAttributes.push_back(Vertex::GetAttributeDescription(
			3, 4, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 4));
		_vertexAttributes.push_back(Vertex::GetAttributeDescription(
			3, 5, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 8));
		_vertexAttributes.push_back(Vertex::GetAttributeDescription(
			3, 6, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 12));

		return Shader::GetVertexInputStateCreateInfo();
	}

	vector<string> SampleShader::GetVertexAttirbuteNames() const
	{
		vector<string> names = { VertexAttributeName::Position, VertexAttributeName::Col, VertexAttributeName::UV };
		return names;
	}

	bool SampleShader::UseInstancing()
	{
		return true;
	}
}
