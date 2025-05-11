#include "stdafx.h"
#include "ResourceCache.h"
#include "Utils/Utility.h"
#include "Assets/Shaders/PBRShader.h"
#include "Assets/Shaders/ShadowShader.h"
#include "Assets/Shaders/SkyboxShader.h"
#include "Assets/Shaders/IrradianceShader.h"
#include "Assets/Shaders/PrefilterShader.h"
#include "Assets/Shaders/BrdfLutShader.h"
#include "Graphics/TransferJob.h"

Core::ResourceCache::ResourceCache(Device& device)
	: _device(device)
{
	ImageCreateInfo imageCreateInfo{};
	imageCreateInfo.filePath = DEFAULT_IMAGE;
	_defaultTexture = RequestTexture(DEFAULT_TEXTURE, imageCreateInfo, DEFAULT_SAMPLER);

	auto& commandBuffer = _device.BeginSingleTimeCommands();

	VkImageJob job(_device, _defaultTexture->GetImage(), _defaultTexture->GetName());
	job.commandBuffer = &commandBuffer;
	job.Execute();

	_device.EndSingleTimeCommands(commandBuffer);
}

Core::ResourceCache::~ResourceCache()
{
	_materials.clear();
	_shaders.clear();
	_images.clear();
	_samplers.clear();
	_textures.clear();
	
	_defaultTexture = nullptr;
}

shared_ptr<Core::Material> Core::ResourceCache::RequestMaterial(const string materialName,
	const string& shaderName)
{
	lock_guard<mutex> guard(_materialMutex);

	auto it = _materials.find(materialName);
	if (it != _materials.end())
	{
		if (auto shared = it->second.lock())
			return shared;
	}

	auto material =
		make_shared<Core::Material>(_device, shaderName, materialName);
	_materials[materialName] = material;

	return material;
}

shared_ptr<Core::Shader> Core::ResourceCache::RequestShader(const string& shaderPath)
{
	lock_guard<mutex> guard(_shaderMutex);

	uint32_t hash = Utility::HashCode(shaderPath.c_str());

	auto it = _shaders.find(hash);
	if (it != _shaders.end())
	{
		if (auto shared = it->second.lock())
			return shared;
	}

	auto shader = CreateShaderInternal(hash);
	shader->Prepare();
	shader->CreatePipelineLayout();
	_shaders[hash] = shader;

	return shader;
}

shared_ptr<Core::Image> Core::ResourceCache::RequestImage(const ImageCreateInfo imageCreateInfo)
{
	lock_guard<mutex> guard(_imageMutex);

	auto it = _images.find(imageCreateInfo.filePath);
	if (it != _images.end())
	{
		if (auto shared = it->second.lock())
			return shared;
	}

	auto image =
		make_shared<Core::Image>(_device, imageCreateInfo);
	_images[imageCreateInfo.filePath] = image;

	return image;
}

shared_ptr<Core::Sampler> Core::ResourceCache::RequestSampler(const SamplerCreateInfo info)
{
	lock_guard<mutex> guard(_samplerMutex);

	auto it = _samplers.find(info);
	if (it != _samplers.end())
	{
		if (auto shared = it->second.lock())
			return shared;
	}

	auto sampler =
		make_shared<Core::Sampler>(_device, info);
	_samplers[info] = sampler;

	return sampler;
}

shared_ptr<Core::Texture> Core::ResourceCache::RequestTexture(const string& textureName,
	const ImageCreateInfo imageCreateInfo, const SamplerCreateInfo samplerCreateInfo)
{
	lock_guard<mutex> guard(_textureMutex);

	string newName = textureName;
	if (newName.empty())
		newName = imageCreateInfo.filePath;

	auto it = _textures.find(newName);
	if (it != _textures.end())
	{
		if (auto shared = it->second.lock())
			return shared;
	}

	auto image = RequestImage(imageCreateInfo);
	auto sampler = RequestSampler(samplerCreateInfo);

	auto texture =
		make_shared<Core::Texture>(newName, image, sampler);
	_textures[newName] = texture;

	return texture;
}

shared_ptr<Core::Texture> Core::ResourceCache::RequestTexture(const string& textureName, const shared_ptr<Core::Image> image, const shared_ptr<Core::Sampler> sampler)
{
	lock_guard<mutex> guard(_textureMutex);

	string newName = textureName;
	if (newName.empty())
		newName = image->GetFilePath();

	auto it = _textures.find(newName);
	if (it != _textures.end())
	{
		if (auto shared = it->second.lock())
			return shared;
	}

	auto texture =
		make_shared<Core::Texture>(newName, image, sampler);
	_textures[newName] = texture;

	return texture;
}

shared_ptr<Core::SubMesh> Core::ResourceCache::RequestSubMesh(const string& name)
{
	lock_guard<mutex> guard(_subMeshMutex);

	auto it = _subMeshes.find(name);
	if (it != _subMeshes.end())
	{
		if (auto shared = it->second.lock())
			return shared;
	}

	auto subMesh = 
		make_shared<Core::SubMesh>(_device, name);
	_subMeshes[name] = subMesh;

	return subMesh;
}

shared_ptr<Core::Shader> Core::ResourceCache::CreateShaderInternal(uint32_t hash)
{
	shared_ptr<Core::Shader> shader;
	
	switch (hash)
	{
	case Utility::HashCode("PBR"):
		shader = make_shared<PBRShader>(_device);
		break;
	case Utility::HashCode("Shadow"):
		shader = make_shared<ShadowShader>(_device);
		break;
	case Utility::HashCode("Skybox"):
		shader = make_shared<SkyboxShader>(_device);
		break;
	case Utility::HashCode("Irradiance"):
		shader = make_shared<IrradianceShader>(_device);
		break;
	case Utility::HashCode("Prefiltered"):
		shader = make_shared<PrefilterShader>(_device);
		break;
	case Utility::HashCode("BRDF"):
		shader = make_shared<BrdfLutShader>(_device);
		break;
	default:
		shader = make_shared<PBRShader>(_device);
		break;
	}

	shader->SetHash(hash);

	return shader;
}
