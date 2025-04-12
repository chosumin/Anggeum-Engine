#include "stdafx.h"
#include "BrdfLutShader.h"

namespace Core
{
	BrdfLutShader::BrdfLutShader(Device& device)
		:Shader(device,
			"shaders/brdf_lut.vert.spv",
			"shaders/brdf_lut.frag.spv")
	{
	}

	type_index BrdfLutShader::GetType()
	{
		return typeid(BrdfLutShader);
	}

	string BrdfLutShader::GetPass()
	{
		return "PreSky";
	}

	vector<string> BrdfLutShader::GetVertexAttirbuteNames() const
	{
		 return vector<string>();
	}

	void BrdfLutShader::Prepare()
	{
	}

	bool BrdfLutShader::UseInstancing()
	{
		return false;
	}
}
