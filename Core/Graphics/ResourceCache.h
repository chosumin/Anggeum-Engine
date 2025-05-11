#pragma once
#include "Graphics/Material.h"
#include "Graphics/SubMesh.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/Image.h"
#include "Graphics/Vulkans/Sampler.h"
#include "Graphics/Vulkans/Texture.h"

#define DEFAULT_SAMPLER SamplerCreateInfo()
#define DEFAULT_IMAGE "Assets/Textures/white.png"
#define DEFAULT_TEXTURE "Default Texture"

namespace Core
{
	class ResourceCache
	{
	public:
		ResourceCache(Device& device);
		~ResourceCache();

		//todo : needs material hash
		shared_ptr<Material> RequestMaterial(const string materialName, 
			const string& shaderName);

		shared_ptr<Shader> RequestShader(const string& shaderPath);

		shared_ptr<Core::Image> RequestImage(const ImageCreateInfo imageCreateInfo);

		shared_ptr<Sampler> RequestSampler(const SamplerCreateInfo info);

		shared_ptr<Texture> RequestTexture(const string& textureName, 
			const ImageCreateInfo imageCreateInfo,
			const SamplerCreateInfo samplerCreateInfo);
		shared_ptr<Texture> RequestTexture(const string& textureName,
			const shared_ptr<Core::Image> image,
			const shared_ptr<Core::Sampler> sampler);
		shared_ptr<Texture> RequestDefaultTexture()
		{
			return _defaultTexture;
		}

		shared_ptr<Core::SubMesh> RequestSubMesh(const string& name);
	private:
		shared_ptr<Shader> CreateShaderInternal(uint32_t hash);
	private:
		Device& _device;
		
		unordered_map<string, weak_ptr<Material>> _materials;
		mutex _materialMutex;

		unordered_map<uint32_t, weak_ptr<Shader>> _shaders;
		mutex _shaderMutex;

		unordered_map<string, weak_ptr<Image>> _images;
		mutex _imageMutex;

		unordered_map<SamplerCreateInfo, weak_ptr<Sampler>, SamplerCreateInfoHasher> _samplers;
		mutex _samplerMutex;

		unordered_map<string, weak_ptr<Texture>> _textures;
		shared_ptr<Texture> _defaultTexture;
		mutex _textureMutex;

		unordered_map<string, weak_ptr<SubMesh>> _subMeshes;
		mutex _subMeshMutex;
	};
}