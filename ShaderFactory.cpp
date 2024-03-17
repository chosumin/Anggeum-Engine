#include "stdafx.h"
#include "ShaderFactory.h"
#include "Shader.h"
#include "SampleShader.h"
#include "Utility.h"
using namespace Core;

mutex _shaderMutex;
unordered_map<uint32_t, Shader*> ShaderFactory::_cache;

Shader* Core::ShaderFactory::CreateShader(Device& device, string name)
{
	lock_guard<mutex> guard(_shaderMutex);

	uint32_t hash = Utility::HashCode(name.c_str());

	Shader* cached = _cache[hash];

	if (cached == nullptr)
	{
		cached = CreateShaderInternal(device, hash);
		_cache[hash] = cached;
	}

	return cached;
}

void Core::ShaderFactory::DeleteCache()
{
	for (auto&& cache : _cache)
	{
		if (cache.second != nullptr)
			delete(cache.second);
	}

	_cache.clear();
}

Shader* Core::ShaderFactory::CreateShaderInternal(Device& device, size_t hash)
{
	switch (hash)
	{
		case Utility::HashCode("Sample"):
			return new SampleShader(device);
		default:
			return new SampleShader(device);
	}
}
