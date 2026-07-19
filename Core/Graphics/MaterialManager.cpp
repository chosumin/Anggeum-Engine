#include "stdafx.h"
#include "MaterialManager.h"
#include "Material.h"
#include "Vulkans/Buffer.h"
#include "Vulkans/MemoryAllocator.h"

using namespace Core;

MaterialManager::MaterialManager(Device& device)
	: _device(device)
{
	for (auto& data : _materialData)
	{
		data = GPUMaterialData{};
	}

	_materialDataBuffer = make_unique<Buffer>(_device,
		sizeof(MaterialTable),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		MemoryType::UNIFORM);

	// Seed the GPU copy so a frame that draws before any material is registered
	// still reads defined data.
	_materialDataBuffer->Update(_materialData);
}

MaterialManager::~MaterialManager() = default;

void MaterialManager::UpdateMaterialData(uint32_t materialIndex)
{
	auto& material = _materials[materialIndex];
	if (material.expired())
		return;

	GPUMaterialData& data = _materialData[materialIndex];
	
	//todo: cast out of this function if material is not PBR
	auto pbrBuffer = material.lock()->GetBufferConst<PBRBuffer>(1);
	if (pbrBuffer)
	{
		data.albedo = pbrBuffer->Albedo;
		data.metallic = pbrBuffer->Metallic;
		data.roughness = pbrBuffer->Roughness;
		data.ao = pbrBuffer->AO;

		data.albedoTextureSet = pbrBuffer->AlbedoTextureSet;
		data.metallicTextureSet = pbrBuffer->MetallicTextureSet;
		data.roughnessTextureSet = pbrBuffer->RoughnessTextureSet;
		data.occlusionTextureSet = pbrBuffer->OcclusionTextureSet;
		data.debugMode = pbrBuffer->DebugMode;

		data.basemapIndex = pbrBuffer->BasemapIndex;
		data.normalmapIndex = pbrBuffer->NormalmapIndex;
		data.metallicRoughnessmapIndex = pbrBuffer->MetallicRoughnessmapIndex;
	}

	data.flags = 1;  // Enabled
}

uint32_t MaterialManager::RegisterMaterial(shared_ptr<Material> material)
{
	uint32_t index;

	if (!_freeIndices.empty())
	{
		index = _freeIndices.back();
		_freeIndices.pop_back();
	}
	else
	{
		if (_materialCount >= MAX_MATERIALS)
		{
			throw runtime_error("Material count exceeded MAX_MATERIALS (256)");
		}
		index = _materialCount++;
	}

	_materials[index] = material;
	material->SetMaterialIndex(index);

	_dirtyMaterials.set(index);
	_anyDirty = true;

	return index;
}

void MaterialManager::UnregisterMaterial(uint32_t materialIndex)
{
	if (materialIndex >= MAX_MATERIALS)
		return;

	_materials[materialIndex].reset();
	_materialData[materialIndex] = GPUMaterialData{};
	_freeIndices.push_back(materialIndex);

	_dirtyMaterials.set(materialIndex);
	_anyDirty = true;
}

void MaterialManager::MarkDirty(uint32_t materialIndex)
{
	if (materialIndex < MAX_MATERIALS)
	{
		_dirtyMaterials.set(materialIndex);
		_anyDirty = true;
	}
}

void MaterialManager::RefreshDirtyMaterials()
{
	if (!_anyDirty)
		return;

	for (uint32_t i = 0; i < _materialCount; ++i)
	{
		if (_dirtyMaterials.test(i))
		{
			UpdateMaterialData(i);
		}
	}

	_dirtyMaterials.reset();
	_anyDirty = false;

	_materialDataBuffer->Update(_materialData);
}