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
		_shaders.clear();
		_images.clear();
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
			make_shared<Core::Material>(_device, shaderName, materialName);
		_materials[materialName] = material;

		if (_device.IsGpuDrivenRenderingEnabled())
		{
			MaterialManager* materialManager = _renderContext->GetMaterialManager();
			uint32_t materialIndex = materialManager->RegisterMaterial(material);
		}

		return material;
	}

	shared_ptr<Shader> ResourceCache::RequestShader(const string& shaderName)
	{
		lock_guard<mutex> guard(_shaderMutex);

		string pass;
		string vertShaderPath, fragShaderPath;

		uint32_t hash = Utility::HashCode(shaderName.c_str());
		GetShaderFiles(hash, pass, vertShaderPath, fragShaderPath);

		auto it = _shaders.find(shaderName);
		if (it != _shaders.end())
		{
			if (auto shared = it->second.lock())
				return shared;
		}

		shared_ptr<Core::Shader> shader;
		if (vertShaderPath.empty() || fragShaderPath.empty())
			shader = make_shared<Shader>(_device, pass,
				shaderName);
		else
			shader = make_shared<Shader>(_device, pass,
				vertShaderPath, fragShaderPath);

		// Set bindless descriptor set layout BEFORE CreatePipelineLayout
		if (_renderContext && _renderContext->HasBindlessSupport() && shader->UsesBindlessTextures())
		{
			auto* bindlessManager = _renderContext->GetBindlessTextureManager();
			shader->SetBindlessDescriptorSetLayout(bindlessManager->GetDescriptorSetLayout());
			
			cout << "Shader '" << shaderName << "' configured with bindless texture support" << endl;
		}

		shader->CreatePipelineLayout();
		_shaders[shaderName] = shader;

		return shader;
	}

	shared_ptr<Shader> ResourceCache::RequestShader(const string& vertPath, const string& fragPath)
	{
		lock_guard<mutex> guard(_shaderMutex);

		string pass = "Geometry";

		string shaderName = vertPath + fragPath; // Create a unique name based on paths

		auto it = _shaders.find(shaderName);
		if (it != _shaders.end())
		{
			if (auto shared = it->second.lock())
				return shared;
		}

		shared_ptr<Core::Shader> shader = make_shared<Shader>(_device, pass,
			vertPath, fragPath);

		// Set bindless descriptor set layout BEFORE CreatePipelineLayout
		if (_renderContext && _renderContext->HasBindlessSupport() && shader->UsesBindlessTextures())
		{
			auto* bindlessManager = _renderContext->GetBindlessTextureManager();
			shader->SetBindlessDescriptorSetLayout(bindlessManager->GetDescriptorSetLayout());
			
			cout << "Shader '" << shaderName << "' configured with bindless texture support" << endl;
		}

		shader->CreatePipelineLayout();
		_shaders[shaderName] = shader;

		return shader;
	}

	shared_ptr<Core::Image> ResourceCache::RequestImage(const ImageCreateInfo imageCreateInfo)
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

		auto image = RequestImage(imageCreateInfo);
		auto sampler = RequestSampler(samplerCreateInfo);

		auto texture =
			make_shared<Core::Texture>(newName, image, sampler);
		_textures[newName] = texture;

		return texture;
	}

	shared_ptr<Core::Texture> ResourceCache::RequestTexture(const string& textureName, const shared_ptr<Core::Image> image, const shared_ptr<Core::Sampler> sampler)
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
			vert = "shaders/pbr.vert.spv";
			frag = "shaders/pbr.frag.spv";
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
