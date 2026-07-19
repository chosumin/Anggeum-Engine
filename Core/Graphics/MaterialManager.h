#pragma once
#include "BufferObjects.h"

namespace Core
{
	class Material;
	class Device;
	class Buffer;

	// GPU-driven rendering resource, alongside MeshBufferManager and
	// BindlessTextureManager: owns the material table and its GPU mirror for the
	// whole application, and hands the buffer out for passes to bind.
	class MaterialManager
	{
	public:
		static constexpr uint32_t MAX_MATERIALS = 256;

		// The GPU-side material table, uploaded as a single uniform block.
		using MaterialTable = array<GPUMaterialData, MAX_MATERIALS>;

		MaterialManager(Device& device);
		~MaterialManager();

		uint32_t RegisterMaterial(shared_ptr<Material> material);
		void UnregisterMaterial(uint32_t materialIndex);

		// Refreshes dirty entries and re-uploads the table if anything changed.
		void RefreshDirtyMaterials();

		const MaterialTable& GetMaterialData() const { return _materialData; }

		// The GPU mirror of the table, bound by the passes that draw.
		Buffer& GetMaterialBuffer() const { return *_materialDataBuffer; }

		uint32_t GetMaterialCount() const { return _materialCount; }

		void MarkDirty(uint32_t materialIndex);

	private:
		void UpdateMaterialData(uint32_t materialIndex);

	private:
		Device& _device;

		MaterialTable _materialData;
		unique_ptr<Buffer> _materialDataBuffer;
		array<weak_ptr<Material>, MAX_MATERIALS> _materials;

		bitset<MAX_MATERIALS> _dirtyMaterials;
		bool _anyDirty = false;

		uint32_t _materialCount = 0;
		vector<uint32_t> _freeIndices;
	};
}