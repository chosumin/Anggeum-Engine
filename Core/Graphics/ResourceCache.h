#pragma once
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Sampler.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Material.h"
#include "Graphics/SubMesh.h"
#include "Graphics/ResourcePool.h"

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
		shared_ptr<Material> RequestOverrideMaterial(const shared_ptr<Material>& source, const string& overrideShaderName);

		Handle<Shader> LoadShader(const string& shaderName);
		Handle<Shader> LoadShader(const string& vertPath, const string& fragPath);
		shared_ptr<Sampler> RequestSampler(const SamplerCreateInfo info);
		shared_ptr<Texture> RequestTexture(const string& textureName,
			const ImageCreateInfo imageCreateInfo,
			const SamplerCreateInfo samplerCreateInfo);
		shared_ptr<Texture> RequestTexture(const string& textureName,
			const ImageCreateInfo imageCreateInfo);
		shared_ptr<Texture> RequestTexture(const string& textureName,
			const ImageCreateInfo imageCreateInfo, const shared_ptr<Sampler> sampler);
		shared_ptr<SubMesh> RequestSubMesh(const string& name);

		shared_ptr<Texture> GetDefaultTexture() { return _defaultTexture; }

	private:
		void GetShaderFiles(const uint32_t hash, string& pass, string& vert, string& frag);
		Handle<Shader> StoreShader(const string& name, shared_ptr<Shader> shader);

	private:
		Device& _device;
		RenderContext* _renderContext = nullptr;

		shared_ptr<Texture> _defaultTexture;

		mutex _materialMutex;
		mutex _shaderMutex;
		mutex _samplerMutex;
		mutex _textureMutex;
		mutex _subMeshMutex;

		unordered_map<string, weak_ptr<Material>> _materials;

		// Shaders: pool-owned, looked up by name for dedup.
		ResourcePool<Shader> _shaderPool;
		unordered_map<string, Handle<Shader>> _shaderHandles;
		unordered_map<SamplerCreateInfo, weak_ptr<Sampler>, SamplerCreateInfoHasher> _samplers;
		unordered_map<string, weak_ptr<Texture>> _textures;
		unordered_map<string, weak_ptr<SubMesh>> _subMeshes;
	};
}