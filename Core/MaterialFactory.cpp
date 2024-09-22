#include "stdafx.h"
#include "MaterialFactory.h"
#include "Material.h"
#include "Utils/Utility.h"
#include "JsonParser.h"
#include "VulkanWrapper/Texture.h"
using namespace Core;

mutex _materialMutex;
unordered_map<uint32_t, Material*> MaterialFactory::_materialCache;

Material* Core::MaterialFactory::CreateMaterial(Device& device, string materialPath)
{
	lock_guard<mutex> guard(_materialMutex);

	uint32_t hash = Utility::HashCode(materialPath.c_str());

	Material* cached = _materialCache[hash];

	if (cached == nullptr)
	{
		cached = Parse(device, materialPath);
		_materialCache[hash] = cached;
	}
	
	return cached;
}

void Core::MaterialFactory::DeleteCache()
{
	//hack : implement after releasing mutex. need to quit thread immediately.
	lock_guard<mutex> guard(_materialMutex);

	for (auto&& cache : _materialCache)
	{
		if (cache.second != nullptr)
			delete(cache.second);
	}

	_materialCache.clear();
}

Material* Core::MaterialFactory::Parse(Device& device, string materialPath)
{
	auto document = JsonParser::LoadDocument(materialPath);

	string shaderPath = (*document)["shader"].GetString();
	
	uint32_t hash = Utility::HashCode(materialPath.c_str());

	Material* material = new Core::Material(device, shaderPath, hash);

	if (document->HasMember("uniforms"))
	{
		const Value& uniforms = (*document)["uniforms"];

		for (SizeType i = 0; i < uniforms.Size(); ++i)
		{
			uint32_t binding = uniforms["binding"].GetUint();
			string format = uniforms["format"].GetString();
			//uniforms["value"].GetFloat
		}
	}

	if (document->HasMember("textures"))
	{
		const Value& textures = (*document)["textures"];

		for (SizeType i = 0; i < textures.Size(); ++i)
		{
			const Value& textureJson = textures[i];
			
			uint32_t binding = textureJson["binding"].GetUint();
			string texturePath = textureJson["path"].GetString();

			auto texture = new Texture(device,
				texturePath, Core::TextureFormat::Rgb_alpha);

			material->SetBuffer(binding, texture);
		}
	}

	return material;
}