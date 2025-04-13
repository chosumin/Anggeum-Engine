#include "stdafx.h"
#include "ShaderFactory.h"
#include "Graphics/Vulkans/Shader.h"
#include "Assets/Shaders/PBRShader.h"
#include "Assets/Shaders/ShadowShader.h"
#include "Assets/Shaders/SkyboxShader.h"
#include "Assets/Shaders/IrradianceShader.h"
#include "Assets/Shaders/PrefilterShader.h"
#include "Assets/Shaders/BrdfLutShader.h"
#include "Utils/Utility.h"
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
		cached->Prepare();
		cached->CreatePipelineLayout();

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
		case Utility::HashCode("PBR"):
			return new PBRShader(device);
		case Utility::HashCode("Shadow"):
			return new ShadowShader(device);
		case Utility::HashCode("Skybox"):
			return new SkyboxShader(device);
		case Utility::HashCode("Irradiance"):
			return new IrradianceShader(device);
		case Utility::HashCode("Prefiltered"):
			return new PrefilterShader(device);
		case Utility::HashCode("BRDF"):
			return new BrdfLutShader(device);
		default:
			return new PBRShader(device);
	}
}
