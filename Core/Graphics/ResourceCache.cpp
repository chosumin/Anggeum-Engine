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

shared_ptr<Core::Material> Core::ResourceCache::RequestMaterial(const string& shaderName)
{
	lock_guard<mutex> guard(_materialMutex);

	uint32_t hash = Utility::HashCode(shaderName.c_str());
	
	auto it = _materials.find(hash);
	if (it != _materials.end())
	{
		if (auto shared = it->second.lock())
			return shared;
	}

	auto material =
		make_shared<Core::Material>(_device, shaderName, hash);
	_materials[hash] = material;

	return material;
}

void Core::ResourceCache::ReleaseMaterial(const shared_ptr<Material> material)
{
	/*lock_guard<mutex> guard(_materialMutex);

	uint32_t hash = material->GetHash();

	if (_materials.find(hash) != _materials.end())
	{
		if (_materials[hash].use_count() <= 0)
		{
			_materials.erase(hash);
		}
	}*/
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

void Core::ResourceCache::ReleaseShader(const shared_ptr<Shader> shader)
{
	/*lock_guard<mutex> guard(_shaderMutex);

	uint32_t hash = shader->GetHash();

	if (_shaders.find(hash) != _shaders.end())
	{
		if (_shaders[hash].use_count() <= 0)
		{
			_shaders.erase(hash);
		}
	}*/
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

void Core::ResourceCache::ReleaseImage(const shared_ptr<Core::Image> image)
{
	/*lock_guard<mutex> guard(_imageMutex);

	string& path = image->GetFilePath();

	if (_images.find(path) != _images.end())
	{
		if (_images[path].use_count() <= 0)
		{
			_images.erase(path);
		}
	}*/
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

void Core::ResourceCache::ReleaseSampler(const shared_ptr<Core::Sampler> sampler)
{
	/*lock_guard<mutex> guard(_samplerMutex);

	auto& info = sampler->GetCreateInfo();

	if (_samplers.find(info) != _samplers.end())
	{
		if (_samplers[info].use_count() <= 0)
		{
			_samplers.erase(info);
		}
	}*/
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

void Core::ResourceCache::ReleaseTexture(const shared_ptr<Core::Texture> texture)
{
	/*lock_guard<mutex> guard(_textureMutex);

	string& path = texture->GetName();

	if (_textures.find(path) != _textures.end())
	{
		if (_textures[path].use_count() <= 0)
		{
			_textures.erase(path);
		}
	}*/
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
