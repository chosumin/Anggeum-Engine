#pragma once
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Sampler.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Material.h"
#include "Graphics/SubMesh.h"

#define DEFAULT_SAMPLER SamplerCreateInfo()
#define DEFAULT_IMAGE "Assets/Textures/white.png"
#define DEFAULT_TEXTURE "Default Texture"

namespace Core
{
	class Device;
	class Image;
	class RenderContext;

	class ResourceCache
	{
	public:
		ResourceCache(Device& device);
		~ResourceCache();

		void Prepare(RenderContext& renderContext);

		shared_ptr<Material> RequestMaterial(const string materialName, const string& shaderName);
		shared_ptr<Shader> RequestShader(const string& shaderName);
		shared_ptr<Shader> RequestShader(const string& vertPath, const string& fragPath);
		shared_ptr<Image> RequestImage(const ImageCreateInfo imageCreateInfo);
		shared_ptr<Sampler> RequestSampler(const SamplerCreateInfo info);
		shared_ptr<Texture> RequestTexture(const string& textureName, const ImageCreateInfo imageCreateInfo,
			const SamplerCreateInfo samplerCreateInfo);
		shared_ptr<Texture> RequestTexture(const string& textureName, const shared_ptr<Image> image,
			const shared_ptr<Sampler> sampler);
		shared_ptr<SubMesh> RequestSubMesh(const string& name);

		shared_ptr<Texture> GetDefaultTexture() { return _defaultTexture; }

	private:
		void GetShaderFiles(const uint32_t hash, string& pass, string& vert, string& frag);

	private:
		Device& _device;
		RenderContext* _renderContext = nullptr;

		shared_ptr<Texture> _defaultTexture;

		mutex _materialMutex;
		mutex _shaderMutex;
		mutex _imageMutex;
		mutex _samplerMutex;
		mutex _textureMutex;
		mutex _subMeshMutex;

		unordered_map<string, weak_ptr<Material>> _materials;
		unordered_map<string, weak_ptr<Shader>> _shaders;
		unordered_map<string, weak_ptr<Image>> _images;
		unordered_map<SamplerCreateInfo, weak_ptr<Sampler>, SamplerCreateInfoHasher> _samplers;
		unordered_map<string, weak_ptr<Texture>> _textures;
		unordered_map<string, weak_ptr<SubMesh>> _subMeshes;
	};
}