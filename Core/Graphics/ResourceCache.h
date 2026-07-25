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
		// Samplers are pool-owned; resolve the handle with handle.Get().
		Handle<Sampler> LoadSampler(const SamplerCreateInfo info);
		// Asset textures are pool-owned; resolve the handle with handle.Get().
		Handle<Texture> LoadTexture(const string& textureName,
			const ImageCreateInfo imageCreateInfo,
			const SamplerCreateInfo samplerCreateInfo);
		Handle<Texture> LoadTexture(const string& textureName,
			const ImageCreateInfo imageCreateInfo);
		Handle<Texture> LoadTexture(const string& textureName,
			const ImageCreateInfo imageCreateInfo, const Handle<Sampler> sampler);

		// Adopts an externally built image (e.g. a GPU-generated volume) into the
		// texture pool. For app-lifetime textures that aren't loaded from a file.
		Handle<Texture> LoadTexture(const string& name,
			unique_ptr<Image> image, Handle<Sampler> sampler);

		shared_ptr<SubMesh> RequestSubMesh(const string& name);

		Handle<Texture> GetDefaultTextureHandle() const { return _defaultTexture; }

	private:
		void GetShaderFiles(const uint32_t hash, string& pass, string& vert, string& frag);
		Handle<Shader> StoreShader(const string& name, shared_ptr<Shader> shader);

	private:
		Device& _device;
		RenderContext* _renderContext = nullptr;

		Handle<Texture> _defaultTexture;

		mutex _materialMutex;
		mutex _shaderMutex;
		mutex _samplerMutex;
		mutex _textureMutex;
		mutex _subMeshMutex;

		unordered_map<string, weak_ptr<Material>> _materials;

		// Shaders: pool-owned, looked up by name for dedup.
		ResourcePool<Shader> _shaderPool;
		unordered_map<string, Handle<Shader>> _shaderHandles;

		// Samplers: pool-owned, looked up by create-info for dedup.
		ResourcePool<Sampler> _samplerPool;
		unordered_map<SamplerCreateInfo, Handle<Sampler>, SamplerCreateInfoHasher> _samplerHandles;

		// Textures: pool-owned, looked up by name for dedup.
		ResourcePool<Texture> _texturePool;
		unordered_map<string, Handle<Texture>> _textureHandles;

		unordered_map<string, weak_ptr<SubMesh>> _subMeshes;
	};
}