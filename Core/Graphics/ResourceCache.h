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

		// Pool-owned; resolve the handle with handle.Get().
		Handle<Material> LoadMaterial(const string materialName, const string& shaderName);

		Handle<Shader> LoadShader(const string& shaderName);
		Handle<Shader> LoadShader(const string& vertPath, const string& fragPath);

		Handle<Sampler> LoadSampler(const SamplerCreateInfo info);

		// Loads a file-based asset texture (ImageCreateInfo.filePath) into the pool
		// and registers it with the bindless array. Pass a sampler handle, or leave
		// it empty to bind none.
		Handle<Texture> LoadTexture(const string& textureName,
			const ImageCreateInfo imageCreateInfo, const Handle<Sampler> sampler = Handle<Sampler>{});

		// Adopts an externally built image (e.g. a GPU-generated volume) into the
		// texture pool. For app-lifetime textures that aren't loaded from a file.
		Handle<Texture> LoadTexture(const string& name,
			unique_ptr<Image> image, Handle<Sampler> sampler);

		// Pool-owned; resolve the handle with handle.Get().
		Handle<SubMesh> LoadSubMesh(const string& name);

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

		// Materials: pool-owned, looked up by name for dedup.
		ResourcePool<Material> _materialPool;
		unordered_map<string, Handle<Material>> _materialHandles;

		// Shaders: pool-owned, looked up by name for dedup.
		ResourcePool<Shader> _shaderPool;
		unordered_map<string, Handle<Shader>> _shaderHandles;

		// Samplers: pool-owned, looked up by create-info for dedup.
		ResourcePool<Sampler> _samplerPool;
		unordered_map<SamplerCreateInfo, Handle<Sampler>, SamplerCreateInfoHasher> _samplerHandles;

		// Textures: pool-owned, looked up by name for dedup.
		ResourcePool<Texture> _texturePool;
		unordered_map<string, Handle<Texture>> _textureHandles;

		// SubMeshes: pool-owned, looked up by name for dedup.
		ResourcePool<SubMesh> _subMeshPool;
		unordered_map<string, Handle<SubMesh>> _subMeshHandles;
	};
}