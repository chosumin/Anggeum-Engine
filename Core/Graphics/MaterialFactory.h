#pragma once

namespace Core
{
	class Material;
	class MaterialFactory
	{
	public:
		static Material* CreateMaterial(Device& device, string materialPath);
		static void DeleteCache();
	private:
		static Material* Parse(Device& device, string materialPath);
	private:
		static unordered_map<uint32_t, Material*> _materialCache;
	};
}

