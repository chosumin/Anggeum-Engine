#pragma once

namespace Core
{
	class Shader;
	class SpirvUtility
	{
	public:
		static vector<unsigned int> GLSLToSPV(VkShaderStageFlagBits shaderStage, 
			const char* shaderCode, const string& shaderPath, bool gpuDrivenEnabled);
		static void SetResources(Shader& shader, VkShaderStageFlagBits shaderStage, const string& shaderPath, const vector<uint32_t>& spirvBinary);
	};
}