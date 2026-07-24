#include "stdafx.h"
#include "ResourceCache.h"
#include "Utils/Utility.h"
#include "Graphics/TransferJob.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/RenderContext.h"

namespace Core
{
	ResourceCache::ResourceCache(Device& device)
		: _device(device)
	{
		ImageCreateInfo imageCreateInfo{};
		imageCreateInfo.filePath = DEFAULT_IMAGE;
		_defaultTexture = RequestTexture(DEFAULT_TEXTURE, imageCreateInfo, DEFAULT_SAMPLER);

		VkImageJob job(_device, _defaultTexture->GetImage(), _defaultTexture->GetName());

		Core::CommandBuffer::ImmediateSubmit(_device, job);
	}

	ResourceCache::~ResourceCache()
	{
		_materials.clear();
		_samplers.clear();
		_textures.clear();
		
		_defaultTexture = nullptr;
	}

	void ResourceCache::Prepare(RenderContext& renderContext)
	{
		_renderContext = &renderContext;
		
		if (_renderContext->HasBindlessSupport())
		{
			cout << "ResourceCache: Bindless texture support enabled" << endl;
		}
	}

	shared_ptr<Material> ResourceCache::RequestMaterial(const string materialName,
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
			make_shared<Core::Material>(_device, LoadShader(shaderName), materialName);
		_materials[materialName] = material;

		MaterialManager* materialManager = _renderContext->GetMaterialManager();
		materialManager->RegisterMaterial(material);

		return material;
	}

	shared_ptr<Material> ResourceCache::RequestOverrideMaterial(const shared_ptr<Material>& source, const string& overrideShaderName)
	{
		lock_guard<mutex> guard(_materialMutex);

		// Unique name: "originalName@synthesizeShader"
		string overrideName = source->GetName() + "@" + overrideShaderName;

		auto it = _materials.find(overrideName);
		if (it != _materials.end())
		{
			if (auto shared = it->second.lock())
				return shared;
		}

		// Copy original material (preserves PBR data, bindless handles, etc.)
		auto overrideMaterial = make_shared<Material>(*source);
		overrideMaterial->SetShader(LoadShader(overrideShaderName));

		_materials[overrideName] = overrideMaterial;

		// Register to get a valid materialIndex
		MaterialManager* materialManager = _renderContext->GetMaterialManager();
		materialManager->RegisterMaterial(overrideMaterial);

		return overrideMaterial;
	}

	Handle<Shader> ResourceCache::LoadShader(const string& shaderName)
	{
		lock_guard<mutex> guard(_shaderMutex);

		auto it = _shaderHandles.find(shaderName);
		if (it != _shaderHandles.end() && _shaderPool.IsAlive(it->second))
			return it->second;

		string pass;
		string vertShaderPath, fragShaderPath;

		uint32_t hash = Utility::HashCode(shaderName.c_str());
		GetShaderFiles(hash, pass, vertShaderPath, fragShaderPath);

		shared_ptr<Core::Shader> shader;
		if (vertShaderPath.empty() || fragShaderPath.empty())
			shader = make_shared<Shader>(_device, pass, shaderName);
		else
			shader = make_shared<Shader>(_device, pass, vertShaderPath, fragShaderPath);

		return StoreShader(shaderName, std::move(shader));
	}

	Handle<Shader> ResourceCache::LoadShader(const string& vertPath, const string& fragPath)
	{
		lock_guard<mutex> guard(_shaderMutex);

		string name = vertPath + fragPath;

		auto it = _shaderHandles.find(name);
		if (it != _shaderHandles.end() && _shaderPool.IsAlive(it->second))
			return it->second;

		auto shader = make_shared<Shader>(_device, "Geometry", vertPath, fragPath);

		return StoreShader(name, std::move(shader));
	}

	Handle<Shader> ResourceCache::StoreShader(const string& name, shared_ptr<Shader> shader)
	{
		// Set bindless descriptor set layout BEFORE CreatePipelineLayout
		if (_renderContext && _renderContext->HasBindlessSupport() && shader->UsesBindlessTextures())
		{
			auto* bindlessManager = _renderContext->GetBindlessTextureManager();
			shader->SetBindlessDescriptorSetLayout(bindlessManager->GetDescriptorSetLayout());

			cout << "Shader '" << name << "' configured with bindless texture support" << endl;
		}

		shader->CreatePipelineLayout();

		Handle<Shader> handle = _shaderPool.Add(std::move(shader));
		_shaderHandles[name] = handle;
		return handle;
	}

	shared_ptr<Core::Sampler> ResourceCache::RequestSampler(const SamplerCreateInfo info)
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

	shared_ptr<Core::Texture> ResourceCache::RequestTexture(const string& textureName,
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

		auto image = make_unique<Core::Image>(_device, imageCreateInfo);
		auto sampler = RequestSampler(samplerCreateInfo);

		auto texture =
			make_shared<Core::Texture>(newName, std::move(image), sampler);
		_textures[newName] = texture;

		return texture;
	}

	shared_ptr<Texture> ResourceCache::RequestTexture(const string& textureName, const ImageCreateInfo imageCreateInfo)
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

		auto image = make_unique<Core::Image>(_device, imageCreateInfo);

		auto texture =
			make_shared<Core::Texture>(newName, std::move(image), nullptr);
		_textures[newName] = texture;

		return texture;
	}

	shared_ptr<Core::Texture> ResourceCache::RequestTexture(const string& textureName, const ImageCreateInfo imageCreateInfo, const shared_ptr<Core::Sampler> sampler)
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

		auto image = make_unique<Core::Image>(_device, imageCreateInfo);

		auto texture =
			make_shared<Core::Texture>(newName, std::move(image), sampler);
		_textures[newName] = texture;

		return texture;
	}

	shared_ptr<Core::SubMesh> ResourceCache::RequestSubMesh(const string& name)
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

	void Core::ResourceCache::GetShaderFiles(const uint32_t hash,
		string& pass, string& vert, string& frag)
	{
		switch (hash)
		{
		case Utility::HashCode("PBR"):
			pass = "Geometry";
			vert = "shaders/lit.vert.spv";
			frag = "shaders/lit.frag.spv";
			break;
		case Utility::HashCode("Shadow"):
			pass = "Shadow";
			vert = "shaders/shadow.vert.spv";
			frag = "shaders/shadow.frag.spv";
			break;
		case Utility::HashCode("Depth"):
			pass = "Depth";
			vert = "shaders/depth.vert.spv";
			frag = "shaders/shadow.frag.spv";
			break;
		case Utility::HashCode("DepthNormal"):
			pass = "Depth";
			vert = "shaders/depthNormal.vert.spv";
			frag = "shaders/depthNormal.frag.spv";
			break;
		case Utility::HashCode("Skybox"):
			pass = "Skybox";
			vert = "shaders/skybox.vert.spv";
			frag = "shaders/skybox.frag.spv";
			break;
		case Utility::HashCode("Irradiance"):
			pass = "PreSky";
			vert = "shaders/filtercube.vert.spv";
			frag = "shaders/irradiance.frag.spv";
			break;
		case Utility::HashCode("Prefiltered"):
			pass = "PreSky";
			vert = "shaders/filtercube.vert.spv";
			frag = "shaders/prefilter.frag.spv";
			break;
		case Utility::HashCode("BRDF"):
			pass = "PreSky";
			vert = "shaders/brdf_lut.vert.spv";
			frag = "shaders/brdf_lut.frag.spv";
			break;
		default:
			pass = "Geometry";
			//Default is compute shader.
			break;
		}
	}
}
