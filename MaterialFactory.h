#pragma once

namespace Core
{
	enum class MaterialType
	{
		BASE,
		SAMPLE,
		SHADOW,
	};

	class Material;
	class MaterialFactory
	{
	public:
		static Material* CreateMaterial(Device& device, MaterialType material);
		static void DeleteCache();
	private:
		static Material* CreateMaterialInternal(Device& device, MaterialType material);
	private:
		static unordered_map<MaterialType, Material*> _materialCache;
	};
}

