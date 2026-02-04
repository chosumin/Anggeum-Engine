#pragma once
#include "BufferObjects.h"

namespace Core
{
	class Material;

	class MaterialManager
	{
	public:
		static constexpr uint32_t MAX_MATERIALS = 256;

		MaterialManager();
		~MaterialManager() = default;

		uint32_t RegisterMaterial(shared_ptr<Material> material);
		void UnregisterMaterial(uint32_t materialIndex);

		// Update dirty materials (only CPU data)
		void RefreshDirtyMaterials();

		// RenderFrame copies this data
		const GPUMaterialData* GetMaterialData() const { return _materialData.data(); }
		static constexpr size_t GetMaterialDataSize() { return sizeof(GPUMaterialData) * MAX_MATERIALS; }

		uint32_t GetMaterialCount() const { return _materialCount; }

		void MarkDirty(uint32_t materialIndex);

	private:
		void UpdateMaterialData(uint32_t materialIndex);

	private:
		array<GPUMaterialData, MAX_MATERIALS> _materialData;
		array<weak_ptr<Material>, MAX_MATERIALS> _materials;

		bitset<MAX_MATERIALS> _dirtyMaterials;
		bool _anyDirty = false;

		uint32_t _materialCount = 0;
		vector<uint32_t> _freeIndices;
	};
}