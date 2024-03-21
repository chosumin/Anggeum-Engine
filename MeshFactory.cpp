#include "stdafx.h"
#include "MeshFactory.h"
#include "Mesh.h"
using namespace Core;

mutex _meshMutex;
unordered_map<string, Mesh*> MeshFactory::_meshCache;

Mesh* Core::MeshFactory::CreateMesh(Device& device, string path)
{
	lock_guard<mutex> guard(_meshMutex);

	Mesh* cached = _meshCache[path];

	if (cached == nullptr)
	{
		cached = new Mesh(device, path);
		_meshCache[path] = cached;
	}

	return cached;
}

void Core::MeshFactory::DeleteCache()
{
	for (auto&& cache : _meshCache)
	{
		if (cache.second != nullptr)
			delete(cache.second);
	}

	_meshCache.clear();
}