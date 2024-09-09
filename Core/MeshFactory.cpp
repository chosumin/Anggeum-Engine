#include "stdafx.h"
#include "MeshFactory.h"
#include "Mesh.h"
using namespace Core;

mutex _meshMutex;
unordered_map<string, Mesh*> MeshFactory::_meshCache;
unordered_map<int, Mesh*> MeshFactory::_polygonCache;

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

Mesh* Core::MeshFactory::CreateMesh(Device& device, int polygonType)
{
	lock_guard<mutex> guard(_meshMutex);

	Mesh* cached = _polygonCache[polygonType];

	if (cached == nullptr)
	{
		cached = new Mesh(device, polygonType);
		_polygonCache[polygonType] = cached;
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

	for (auto&& cache : _polygonCache)
	{
		if (cache.second != nullptr)
			delete(cache.second);
	}

	_polygonCache.clear();
}