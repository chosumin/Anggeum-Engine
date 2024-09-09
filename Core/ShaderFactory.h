#pragma once

namespace Core
{
	class Shader;
	class ShaderFactory
	{
	public:
		static Shader* CreateShader(Device& device, string name);
		static void DeleteCache();
	private:
		static Shader* CreateShaderInternal(
			Device& device, size_t hash);
	private:
		static unordered_map<uint32_t, Shader*> _cache;
	};
}

