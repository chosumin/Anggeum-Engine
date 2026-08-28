#include "stdafx.h"
#include "ResourceManager.h"
#include "Utils/Utility.h"
#include "Graphics/TextureUpload.h"
#include "Graphics/GeometryUpload.h"
#include "Graphics/TextureUpload.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/RenderContext.h"
#include "Graphics/AssetStreamer.h"

namespace Core
{
	ResourceManager::ResourceManager(Device& device)
		: _device(device)
	{
		ImageCreateDesc imageCreateInfo{};
		imageCreateInfo.filePath = DEFAULT_IMAGE;
		_defaultTexture = LoadTexture(DEFAULT_TEXTURE, imageCreateInfo, LoadSampler(DEFAULT_SAMPLER));

		auto& defaultTex = _defaultTexture.Get();
		TextureUploadJob job(_device, defaultTex, defaultTex.GetName());

		Core::CommandBuffer::ImmediateSubmit(_device, job);
	}

	ResourceManager::~ResourceManager() = default;

	void ResourceManager::Prepare(RenderContext& renderContext, AssetStreamer& assetStreamer)
	{
		_renderContext = &renderContext;
		_streamer = &assetStreamer;
		
		if (_renderContext->HasBindlessSupport())
		{
			cout << "ResourceManager: Bindless texture support enabled" << endl;
		}
	}

	Handle<Material> ResourceManager::LoadMaterial(const string materialName,
		const string& shaderName)
	{
		lock_guard<mutex> guard(_materialMutex);

		auto it = _materialHandles.find(materialName);
		if (it != _materialHandles.end() && _materialPool.IsAlive(it->second))
			return it->second;

		auto material =
			make_shared<Core::Material>(_device, *this, LoadShader(shaderName), materialName);

		Handle<Material> handle = _materialPool.Add(material);
		_materialHandles[materialName] = handle;

		MaterialManager* materialManager = _renderContext->GetMaterialManager();
		materialManager->RegisterMaterial(handle);

		return handle;
	}

	Handle<Shader> ResourceManager::LoadShader(const string& shaderName)
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

	Handle<Pipeline> ResourceManager::LoadComputePipeline(const string& shaderName)
	{
		// LoadShader takes its own lock, so it runs before this one is held.
		Handle<Shader> shader = LoadShader(shaderName);

		lock_guard<mutex> guard(_computePipelineMutex);

		auto it = _computePipelineHandles.find(shaderName);
		if (it != _computePipelineHandles.end() && _computePipelinePool.IsAlive(it->second))
			return it->second;

		Handle<Pipeline> handle = _computePipelinePool.Add(
			make_shared<Pipeline>(_device, shader.Get()));
		_computePipelineHandles[shaderName] = handle;

		return handle;
	}

	Handle<Shader> ResourceManager::LoadShader(const string& vertPath, const string& fragPath)
	{
		lock_guard<mutex> guard(_shaderMutex);

		string name = vertPath + fragPath;

		auto it = _shaderHandles.find(name);
		if (it != _shaderHandles.end() && _shaderPool.IsAlive(it->second))
			return it->second;

		auto shader = make_shared<Shader>(_device, "Geometry", vertPath, fragPath);

		return StoreShader(name, std::move(shader));
	}

	Handle<Shader> ResourceManager::StoreShader(const string& name, shared_ptr<Shader> shader)
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

	Handle<Sampler> ResourceManager::LoadSampler(const SamplerCreateInfo info)
	{
		lock_guard<mutex> guard(_samplerMutex);

		auto it = _samplerHandles.find(info);
		if (it != _samplerHandles.end() && _samplerPool.IsAlive(it->second))
			return it->second;

		Handle<Sampler> handle = _samplerPool.Add(make_shared<Core::Sampler>(_device, info));
		_samplerHandles[info] = handle;
		return handle;
	}

	Handle<Texture> ResourceManager::LoadTexture(const string& textureName, const ImageCreateDesc imageCreateInfo, const Handle<Sampler> sampler)
	{
		lock_guard<mutex> guard(_textureMutex);

		string newName = textureName;
		if (newName.empty())
			newName = imageCreateInfo.filePath;

		auto it = _textureHandles.find(newName);
		if (it != _textureHandles.end() && _texturePool.IsAlive(it->second))
			return it->second;

		auto image = make_unique<Core::Image>(_device, imageCreateInfo);
		auto texture = make_shared<Core::Texture>(newName, std::move(image), sampler);

		Handle<Texture> handle = _texturePool.Add(texture);
		_textureHandles[newName] = handle;

		// Only the ctor's default texture takes that path. 
		// The ctor needs neither the queue nor the Loading state.
		if (_renderContext != nullptr)
		{
			if (_renderContext->HasBindlessSupport())
			{
				auto* bindlessManager = _renderContext->GetBindlessTextureManager();
				uint32_t bindlessIndex = bindlessManager->RegisterTexture(handle);
				texture->SetBindlessIndex(bindlessIndex);
			}

			handle.SetLoading();
			_streamer->Push({ handle, imageCreateInfo.filePath,
				Image::QueryStagingBytes(imageCreateInfo.filePath) });
		}

		return handle;
	}

	Handle<Texture> ResourceManager::LoadTexture(const string& name,
		unique_ptr<Image> image, Handle<Sampler> sampler)
	{
		lock_guard<mutex> guard(_textureMutex);

		auto texture = make_shared<Core::Texture>(name, std::move(image), sampler);

		// Re-creation (e.g. SDF regenerate) orphans the previous slot rather than
		// freeing it, since an in-flight frame may still reference the old texture.
		Handle<Texture> handle = _texturePool.Add(texture);
		_textureHandles[name] = handle;
		return handle;
	}

	// Index count is implied by the data, so callers don't have to pass it separately.
	static uint32_t IndexCountOf(const SubMeshGeometry& geometry)
	{
		if (!geometry.hasIndex)
			return 0;

		uint32_t stride = (geometry.indexType == VK_INDEX_TYPE_UINT16)
			? sizeof(uint16_t) : sizeof(uint32_t);
		return static_cast<uint32_t>(geometry.indexData.size() / stride);
	}

	Handle<Core::SubMesh> ResourceManager::LoadSubMesh(const string& name, SubMeshGeometry&& geometry)
	{
		lock_guard<mutex> guard(_subMeshMutex);

		auto it = _subMeshHandles.find(name);
		if (it != _subMeshHandles.end() && _subMeshPool.IsAlive(it->second))
			return it->second;

		auto subMesh = make_shared<Core::SubMesh>(_device, name);
		subMesh->SetIndexCount(IndexCountOf(geometry));

		Handle<SubMesh> handle = _subMeshPool.Add(subMesh);
		_subMeshHandles[name] = handle;

		GeometryCopyBatch batch;
		batch.debugName = "Geometry_" + name;
		batch.subMesh = handle;

		// Scanning every vertex for bounds is the expensive part, so the upload job
		// does it on a worker thread and reports back here.
		batch.boundsTarget = subMesh.get();

		auto* meshBufferManager = _renderContext->GetMeshBufferManager();
		subMesh->SetAllocation(meshBufferManager->AllocateGeometry(geometry, batch.copies));

		if (!batch.copies.empty())
		{
			handle.SetLoading();
			_streamer->Push(move(batch));
		}

		return handle;
	}

	Handle<Core::SubMesh> ResourceManager::LoadStandaloneSubMesh(const string& name, SubMeshGeometry&& geometry)
	{
		Handle<SubMesh> handle;
		Core::SubMesh* subMesh = nullptr;
		{
			lock_guard<mutex> guard(_subMeshMutex);

			auto it = _subMeshHandles.find(name);
			if (it != _subMeshHandles.end() && _subMeshPool.IsAlive(it->second))
				return it->second;

			auto created = make_shared<Core::SubMesh>(_device, name);
			created->SetIndexCount(IndexCountOf(geometry));

			subMesh = created.get();
			handle = _subMeshPool.Add(created);
			_subMeshHandles[name] = handle;
		}

		// Not part of the global storage, so this geometry gets its own buffers.
		// LoadBuffer takes its own lock, hence outside the guard above.
		GeometryCopyBatch batch;
		batch.debugName = "Standalone_" + name;
		batch.subMesh = handle;

		for (auto& attr : geometry.attributes)
		{
			auto buffer = LoadBuffer(
				{ attr.data.size(),
				  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				  MemoryType::DEVICE_LOCAL },
				name + attr.name);
			subMesh->SetVertexBuffer(attr.name, buffer);
			batch.copies.push_back({ buffer, move(attr.data), 0 });
		}

		if (geometry.hasIndex)
		{
			auto buffer = LoadBuffer(
				{ geometry.indexData.size(),
				  VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
				  MemoryType::DEVICE_LOCAL },
				name + " index");
			subMesh->SetIndexBuffer(buffer, geometry.indexType);
			batch.copies.push_back({ buffer, move(geometry.indexData), 0 });
		}

		if (!batch.copies.empty())
		{
			handle.SetLoading();
			_streamer->Push(move(batch));
		}

		return handle;
	}

	Handle<Buffer> ResourceManager::LoadBuffer(const BufferDesc& desc, const string& debugName)
	{
		lock_guard<mutex> guard(_bufferMutex);

		auto buffer = make_shared<Buffer>(_device, desc.size, desc.usage, desc.memoryType);

		if (!debugName.empty())
		{
			_device.GetDebugUtils().SetObjectName(VK_OBJECT_TYPE_BUFFER,
				(uint64_t)buffer->GetBuffer(), debugName.c_str());
		}

		return _bufferPool.Add(buffer);
	}

	void ResourceManager::ResizeBuffer(Handle<Buffer> handle, const BufferDesc& desc, const string& debugName)
	{
		lock_guard<mutex> guard(_bufferMutex);

		auto buffer = make_shared<Buffer>(_device, desc.size, desc.usage, desc.memoryType);

		if (!debugName.empty())
		{
			_device.GetDebugUtils().SetObjectName(VK_OBJECT_TYPE_BUFFER,
				(uint64_t)buffer->GetBuffer(), debugName.c_str());
		}

		_bufferPool.Replace(handle, buffer);
	}

	void Core::ResourceManager::GetShaderFiles(const uint32_t hash,
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
			// Shares lit.vert with the geometry pass: one vertex shader in both
			// pipelines is what makes the prepass depth exactly re-testable.
			vert = "shaders/lit.vert.spv";
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
		case Utility::HashCode("Terrain"):
			pass = "Terrain";
			vert = "shaders/Terrain/terrain.vert.spv";
			frag = "shaders/Terrain/terrain.frag.spv";
			break;
		case Utility::HashCode("TerrainDepth"):
			pass = "Terrain";
			vert = "shaders/Terrain/terrain.vert.spv";
			frag = "shaders/Terrain/terrainDepth.frag.spv";
			break;
		default:
			pass = "Geometry";
			//Default is compute shader.
			break;
		}
	}
}
