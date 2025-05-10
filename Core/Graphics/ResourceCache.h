#pragma once
#include "Graphics/Material.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/Image.h"
#include "Graphics/Vulkans/Sampler.h"
#include "Graphics/Vulkans/Texture.h"

#define DEFAULT_SAMPLER SamplerCreateInfo()
#define DEFAULT_IMAGE "Assets/Textures/white.png"
#define DEFAULT_TEXTURE "Default Texture"

namespace Core
{
	class Material;
	class Shader;
	class Image;
	class Texture;
	class Sampler;
	class ResourceCache
	{
	public:
		ResourceCache(Device& device);
		~ResourceCache();

		//todo : needs material hash
		shared_ptr<Material> RequestMaterial(const string& shaderName);
		void ReleaseMaterial(const shared_ptr<Material> material);

		shared_ptr<Shader> RequestShader(const string& shaderPath);
		void ReleaseShader(const shared_ptr<Shader> shader);

		shared_ptr<Core::Image> RequestImage(const ImageCreateInfo imageCreateInfo);
		void ReleaseImage(const shared_ptr<Core::Image> image);

		shared_ptr<Sampler> RequestSampler(const SamplerCreateInfo info);
		void ReleaseSampler(const shared_ptr<Core::Sampler> sampler);

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

		void ReleaseTexture(const shared_ptr<Core::Texture> texture);
	private:
		shared_ptr<Shader> CreateShaderInternal(uint32_t hash);
	private:
		Device& _device;
		
		unordered_map<uint32_t, weak_ptr<Material>> _materials;
		mutex _materialMutex;

		unordered_map<uint32_t, weak_ptr<Shader>> _shaders;
		mutex _shaderMutex;

		unordered_map<string, weak_ptr<Image>> _images;
		mutex _imageMutex;

		unordered_map<SamplerCreateInfo, weak_ptr<Sampler>, SamplerCreateInfoHasher> _samplers;
		mutex _samplerMutex;

		unordered_map<string, weak_ptr<Texture>> _textures;
		mutex _textureMutex;

		shared_ptr<Texture> _defaultTexture;
	};
}