#pragma once

struct TBuiltInResource;

namespace Core
{
	class SpirvCompiler
	{
	public:
		static vector<unsigned int> GLSLToSPV(VkShaderStageFlagBits shaderStage, 
			const char* shaderCode, const string& shaderPath);
	};
}