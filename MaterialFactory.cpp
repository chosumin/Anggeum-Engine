#include "stdafx.h"
#include "MaterialFactory.h"
#include "Material.h"
#include "SampleMaterial.h"
using namespace Core;

mutex _materialMutex;
unordered_map<MaterialType, Material*> MaterialFactory::_materialCache;

Material* Core::MaterialFactory::CreateMaterial(Device& device, MaterialType material)
{
	lock_guard<mutex> guard(_materialMutex);

	Material* cached = _materialCache[material];

	if (cached == nullptr)
	{
		cached = CreateMaterialInternal(device, material);
		_materialCache[material] = cached;
	}
	
	return cached;
}

void Core::MaterialFactory::DeleteCache()
{
	//hack : implement after releasing mutex. need to quit thread immediately.

	for (auto&& cache : _materialCache)
	{
		if (cache.second != nullptr)
			delete(cache.second);
	}

	_materialCache.clear();
}

Material* Core::MaterialFactory::CreateMaterialInternal(Device& device, MaterialType material)
{
	switch (material)
	{
		case Core::MaterialType::SAMPLE:
			return new SampleMaterial(device);
		default:
			return new SampleMaterial(device);
	}
}
