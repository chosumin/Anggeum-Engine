#include "stdafx.h"
#include "PrefilterShader.h"

namespace Core
{
	PrefilterShader::PrefilterShader(Device& device)
		:Shader(device,
			"shaders/filtercube.vert.spv",
			"shaders/prefilter.frag.spv")
	{
		AddTextureBufferLayoutBinding(0, VK_SHADER_STAGE_FRAGMENT_BIT);
	}

	type_index PrefilterShader::GetType()
	{
		return typeid(PrefilterShader);
	}

	string PrefilterShader::GetPass()
	{
		return "PreSky";
	}

	vector<string> PrefilterShader::GetVertexAttirbuteNames() const
	{
		vector<string> names = { VertexAttributeName::Position };
		return names;
	}

	void PrefilterShader::Prepare()
	{
		_vertexBindings = {
			Vertex::GetBindingDescription(
		0, 12, VK_VERTEX_INPUT_RATE_VERTEX),
		};

		_vertexAttributes.push_back(Vertex::GetAttributeDescription(
			0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0));

		AddPushConstantsRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(mat4));
		AddPushConstantsRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(float) * 2);
	}

	bool PrefilterShader::UseInstancing()
	{
		return false;
	}
}
