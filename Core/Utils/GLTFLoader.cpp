#include "stdafx.h"
#include "GLTFLoader.h"
#include "Log.h"
#include "Foundation/Scene.h"
#include "Foundation/Entity.h"
#include "Graphics/TransferJob.h"
#include "Graphics/TransferContext.h"
#include "Graphics/Vulkans/Image.h"
#include "Graphics/Vulkans/Sampler.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Vertex.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Material.h"
#include "Graphics/SubMesh.h"
#include "Graphics/ResourceCache.h"
#include "Components/Mesh.h"
#include "Components/PerspectiveCamera.h"
#include "Components/FreeCamera.h"
#include "Components/Light.h"
#include "Utils/Utility.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ThirdParties/tinygltf/tiny_gltf.h"

inline VkFilter FindMinFilter(int minFilter)
{
	switch (minFilter)
	{
	case TINYGLTF_TEXTURE_FILTER_NEAREST:
	case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
	case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
		return VK_FILTER_NEAREST;
	case TINYGLTF_TEXTURE_FILTER_LINEAR:
	case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
	case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
		return VK_FILTER_LINEAR;
	default:
		return VK_FILTER_LINEAR;
	}
};

inline VkFilter FindMagFilter(int magFilter)
{
	switch (magFilter)
	{
	case TINYGLTF_TEXTURE_FILTER_NEAREST:
		return VK_FILTER_NEAREST;
	case TINYGLTF_TEXTURE_FILTER_LINEAR:
		return VK_FILTER_LINEAR;
	default:
		return VK_FILTER_LINEAR;
	}
};

inline VkSamplerMipmapMode FindMipmapMode(int minFilter)
{
	switch (minFilter)
	{
	case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
	case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
		return VK_SAMPLER_MIPMAP_MODE_NEAREST;
	case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
	case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
		return VK_SAMPLER_MIPMAP_MODE_LINEAR;
	default:
		return VK_SAMPLER_MIPMAP_MODE_LINEAR;
	}
};

inline VkSamplerAddressMode FindWrapMode(int wrap)
{
	switch (wrap)
	{
	case TINYGLTF_TEXTURE_WRAP_REPEAT:
		return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
		return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
		return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	default:
		return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}
};

inline bool NeedSRGB(const std::string& name)
{
	// The gltf spec states that the base and emissive textures MUST be encoded with the sRGB
	// transfer function. All other texture types are linear.
	if (name == "baseColorTexture" || name == "emissiveTexture")
		return true;

	// metallicRoughnessTexture, normalTexture & occlusionTexture must be linear
	assert(name == "metallicRoughnessTexture" || name == "normalTexture" || name == "occlusionTexture");
	
	return false;
}

inline vector<uint8_t> GetAttributeData(const tinygltf::Model* model, uint32_t accessorId)
{
	assert(accessorId < model->accessors.size());
	auto& accessor = model->accessors[accessorId];
	assert(accessor.bufferView < model->bufferViews.size());
	auto& bufferView = model->bufferViews[accessor.bufferView];
	assert(bufferView.buffer < model->buffers.size());
	auto& buffer = model->buffers[bufferView.buffer];

	size_t stride = accessor.ByteStride(bufferView);
	size_t startByte = accessor.byteOffset + bufferView.byteOffset;
	size_t endByte = startByte + accessor.count * stride;

	return { buffer.data.begin() + startByte, buffer.data.begin() + endByte };
};

inline VkFormat GetAttributeFormat(const tinygltf::Model* model, uint32_t accessorId)
{
	assert(accessorId < model->accessors.size());
	auto& accessor = model->accessors[accessorId];

	VkFormat format;

	switch (accessor.componentType)
	{
		case TINYGLTF_COMPONENT_TYPE_BYTE:
		{
			static const std::map<int, VkFormat> mappedFormat = { {TINYGLTF_TYPE_SCALAR, VK_FORMAT_R8_SINT},
																	{TINYGLTF_TYPE_VEC2, VK_FORMAT_R8G8_SINT},
																	{TINYGLTF_TYPE_VEC3, VK_FORMAT_R8G8B8_SINT},
																	{TINYGLTF_TYPE_VEC4, VK_FORMAT_R8G8B8A8_SINT} };

			format = mappedFormat.at(accessor.type);

			break;
		}
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
		{
			static const std::map<int, VkFormat> mappedFormat = { {TINYGLTF_TYPE_SCALAR, VK_FORMAT_R8_UINT},
																  {TINYGLTF_TYPE_VEC2, VK_FORMAT_R8G8_UINT},
																  {TINYGLTF_TYPE_VEC3, VK_FORMAT_R8G8B8_UINT},
																  {TINYGLTF_TYPE_VEC4, VK_FORMAT_R8G8B8A8_UINT} };

			static const std::map<int, VkFormat> mappedFormatNormalized = { {TINYGLTF_TYPE_SCALAR, VK_FORMAT_R8_UNORM},
																			{TINYGLTF_TYPE_VEC2, VK_FORMAT_R8G8_UNORM},
																			{TINYGLTF_TYPE_VEC3, VK_FORMAT_R8G8B8_UNORM},
																			{TINYGLTF_TYPE_VEC4, VK_FORMAT_R8G8B8A8_UNORM} };

			if (accessor.normalized)
			{
				format = mappedFormatNormalized.at(accessor.type);
			}
			else
			{
				format = mappedFormat.at(accessor.type);
			}

			break;
		}
		case TINYGLTF_COMPONENT_TYPE_SHORT:
		{
			static const std::map<int, VkFormat> mappedFormat = { {TINYGLTF_TYPE_SCALAR, VK_FORMAT_R8_SINT},
																  {TINYGLTF_TYPE_VEC2, VK_FORMAT_R8G8_SINT},
																  {TINYGLTF_TYPE_VEC3, VK_FORMAT_R8G8B8_SINT},
																  {TINYGLTF_TYPE_VEC4, VK_FORMAT_R8G8B8A8_SINT} };

			format = mappedFormat.at(accessor.type);

			break;
		}
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
		{
			static const std::map<int, VkFormat> mappedFormat = { {TINYGLTF_TYPE_SCALAR, VK_FORMAT_R16_UINT},
																  {TINYGLTF_TYPE_VEC2, VK_FORMAT_R16G16_UINT},
																  {TINYGLTF_TYPE_VEC3, VK_FORMAT_R16G16B16_UINT},
																  {TINYGLTF_TYPE_VEC4, VK_FORMAT_R16G16B16A16_UINT} };

			static const std::map<int, VkFormat> mappedFormatNormalized = { {TINYGLTF_TYPE_SCALAR, VK_FORMAT_R16_UNORM},
																			{TINYGLTF_TYPE_VEC2, VK_FORMAT_R16G16_UNORM},
																			{TINYGLTF_TYPE_VEC3, VK_FORMAT_R16G16B16_UNORM},
																			{TINYGLTF_TYPE_VEC4, VK_FORMAT_R16G16B16A16_UNORM} };

			if (accessor.normalized)
			{
				format = mappedFormatNormalized.at(accessor.type);
			}
			else
			{
				format = mappedFormat.at(accessor.type);
			}

			break;
		}
		case TINYGLTF_COMPONENT_TYPE_INT:
		{
			static const std::map<int, VkFormat> mappedFormat = { {TINYGLTF_TYPE_SCALAR, VK_FORMAT_R32_SINT},
																  {TINYGLTF_TYPE_VEC2, VK_FORMAT_R32G32_SINT},
																  {TINYGLTF_TYPE_VEC3, VK_FORMAT_R32G32B32_SINT},
																  {TINYGLTF_TYPE_VEC4, VK_FORMAT_R32G32B32A32_SINT} };

			format = mappedFormat.at(accessor.type);

			break;
		}
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
		{
			static const std::map<int, VkFormat> mappedFormat = { {TINYGLTF_TYPE_SCALAR, VK_FORMAT_R32_UINT},
																  {TINYGLTF_TYPE_VEC2, VK_FORMAT_R32G32_UINT},
																  {TINYGLTF_TYPE_VEC3, VK_FORMAT_R32G32B32_UINT},
																  {TINYGLTF_TYPE_VEC4, VK_FORMAT_R32G32B32A32_UINT} };

			format = mappedFormat.at(accessor.type);

			break;
		}
		case TINYGLTF_COMPONENT_TYPE_FLOAT:
		{
			static const std::map<int, VkFormat> mappedFormat = { {TINYGLTF_TYPE_SCALAR, VK_FORMAT_R32_SFLOAT},
																  {TINYGLTF_TYPE_VEC2, VK_FORMAT_R32G32_SFLOAT},
																  {TINYGLTF_TYPE_VEC3, VK_FORMAT_R32G32B32_SFLOAT},
																  {TINYGLTF_TYPE_VEC4, VK_FORMAT_R32G32B32A32_SFLOAT} };

			format = mappedFormat.at(accessor.type);

			break;
		}
		default:
		{
			format = VK_FORMAT_UNDEFINED;
			break;
		}
	}

	return format;
};

inline vector<uint8_t> ConvertDataStride(const std::vector<uint8_t>& srcData, uint32_t srcStride, uint32_t dstStride)
{
	auto elementCount = Core::Utility::ToU32(srcData.size()) / srcStride;

	vector<uint8_t> result(elementCount * dstStride);

	for (uint32_t idxSrc = 0, idxDst = 0;
		idxSrc < srcData.size() && idxDst < result.size();
		idxSrc += srcStride, idxDst += dstStride)
	{
		copy(srcData.begin() + idxSrc, srcData.begin() + idxSrc + srcStride, result.begin() + idxDst);
	}

	return result;
}

inline size_t GetAttributeStride(const tinygltf::Model* model, uint32_t accessorId)
{
	assert(accessorId < model->accessors.size());
	auto& accessor = model->accessors[accessorId];
	assert(accessor.bufferView < model->bufferViews.size());
	auto& bufferView = model->bufferViews[accessor.bufferView];

	return accessor.ByteStride(bufferView);
};

Core::GLTFLoader::GLTFLoader(Device& device, Scene& scene, TransferContext& transferContext)
	: _device(device), _scene(scene), _transferContext(transferContext), 
	_resourceCache(device.GetResourceCache())
{
	_model = new tinygltf::Model();
}

Core::GLTFLoader::~GLTFLoader()
{
	delete(_model);
}

void Core::GLTFLoader::LoadScene(string path)
{
	if (LoadFromFile(_model, path) == false)
		return;

	size_t pos = path.find_last_of('/');
	string modelPath = path.substr(0, pos);

	_modelPath = modelPath;
	LoadAssets(modelPath);
}

void Core::GLTFLoader::LoadSkybox(string path)
{
	if (LoadFromFile(_model, "./Assets/Models/cube.gltf") == false)
		return;

	ClearCaches();

	size_t pos = path.find_last_of('/');
	string textureName = path.substr(pos + 1, path.length() - 1);

	//Create a cubemap
	ImageCreateInfo imageCreateInfo{
		path,
		VK_SAMPLE_COUNT_1_BIT,
		VK_IMAGE_VIEW_TYPE_CUBE,
		VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT };

	auto texture = _resourceCache.RequestTexture(textureName,
		imageCreateInfo, DEFAULT_SAMPLER);
	_transferContext.Enqueue(new VkImageJob(_device, texture->GetImage(), path), textureName);

	auto material = _resourceCache.RequestMaterial("skybox", "Skybox");
	material->SetBuffer(1, texture);

	vector<shared_ptr<Material>> materials = { material };
	LoadMeshes(materials);

	LoadNodes();
}

bool Core::GLTFLoader::LoadFromFile(tinygltf::Model* model, const string& path)
{
	string err;
	string warn;

	tinygltf::TinyGLTF loader;

	bool ret = loader.LoadASCIIFromFile(_model, &err, &warn, path);

	if (ret == false)
	{
		LOG("Failed to load gltf file %s.", path.c_str());
	}

	if (err.empty() == false)
	{
		LOG("Error loading gltf file %s.", path.c_str());
	}

	if (warn.empty() == false)
	{
		LOG("Warning loading gltf file %s.", path.c_str());
	}

	return ret;
}

void Core::GLTFLoader::LoadAssets(const string& modelPath)
{
	ClearCaches();

	CheckExtensions();

	LoadLights();

	auto samplers = LoadSamplers();

	auto images = LoadImages(modelPath);

	auto textures = LoadTextures(samplers, images);

	auto materials = LoadMaterials(textures);
	
	LoadMeshes(materials);

	LoadCameras();

	LoadNodes();

	//todo : load animations

	//todo : load scenes
}

void Core::GLTFLoader::CheckExtensions()
{
	for (auto& extention : _model->extensionsUsed)
	{

	}
}

void Core::GLTFLoader::LoadLights()
{
	if (_model->extensions.find(KHR_LIGHTS_PUNCTUAL_EXTENSION) == _model->extensions.end() ||
		!_model->extensions.at(KHR_LIGHTS_PUNCTUAL_EXTENSION).Has("lights"))
	{
		return;
	}

	auto& khrLights = 
		_model->extensions.at(KHR_LIGHTS_PUNCTUAL_EXTENSION).Get("lights");

	for (size_t i = 0; i < khrLights.ArrayLen(); ++i)
	{
		auto& khrLight = khrLights.Get(static_cast<int>(i));

		// Spec states a light has to have a type to be valid
		if (!khrLight.Has("type"))
		{
			LOG("KHR_lights_punctual extension: light {} doesn't have a type!", i);
			throw runtime_error("Couldn't load glTF file, KHR_lights_punctual extension is invalid");
		}

		auto light = 
			make_unique<Core::Light>(khrLight.Get("name").Get<string>());

		LightType type;
		LightProperties properties;

		// Get type
		auto& gltfLightType = khrLight.Get("type").Get<string>();
		
		if (gltfLightType == "point")
		{
			type = LightType::Point;
		}
		else if (gltfLightType == "spot")
		{
			type = LightType::Spot;
		}
		else if (gltfLightType == "directional")
		{
			type = LightType::Directional;
		}
		else
		{
			LOG("KHR_lights_punctual extension: light type '{}' is invalid", gltfLightType);
			throw std::runtime_error("Couldn't load glTF file, KHR_lights_punctual extension is invalid");
		}

		// Get properties
		if (khrLight.Has("color"))
		{
			properties.Color = glm::vec3(
				static_cast<float>(khrLight.Get("color").Get(0).Get<double>()),
				static_cast<float>(khrLight.Get("color").Get(1).Get<double>()),
				static_cast<float>(khrLight.Get("color").Get(2).Get<double>()));
		}

		if (khrLight.Has("intensity"))
		{
			properties.Intensity = 
				static_cast<float>(khrLight.Get("intensity").Get<double>());
		}

		if (type != LightType::Directional)
		{
			properties.Range = static_cast<float>(khrLight.Get("range").Get<double>());
			if (type != LightType::Point)
			{
				if (!khrLight.Has("spot"))
				{
					LOG("KHR_lights_punctual extension: spot light doesn't have a 'spot' property set", gltfLightType);
					throw std::runtime_error("Couldn't load glTF file, KHR_lights_punctual extension is invalid");
				}

				properties.InnerConeAngle = static_cast<float>(khrLight.Get("spot").Get("innerConeAngle").Get<double>());

				if (khrLight.Get("spot").Has("outerConeAngle"))
				{
					properties.OuterConeAngle = static_cast<float>(khrLight.Get("spot").Get("outerConeAngle").Get<double>());
				}
				else
				{
					// Spec states default value is PI/4
					properties.OuterConeAngle = glm::pi<float>() / 4.0f;
				}
			}
		}
		else if (type == LightType::Directional || type == LightType::Spot)
		{
			// The spec states that the light will inherit the transform of the node.
			// The light's direction is defined as the 3-vector (0.0, 0.0, -1.0) and
			// the rotation of the node orients the light accordingly.
			properties.Direction = glm::vec3(0.0f, 0.0f, -1.0f);
		}

		light->SetLightType(type);
		light->SetProperties(properties);

		_lights.push_back(light.get());
		_scene.AddComponent(move(light));
	}
}

vector<shared_ptr<Core::Sampler>> Core::GLTFLoader::LoadSamplers()
{
	size_t size = _model->samplers.size();

	vector<shared_ptr<Core::Sampler>> samplers(size);

	for (size_t i = 0; i < size; ++i)
	{
		auto sampler = _model->samplers[i];

		samplers[i] = LoadSampler(_device, sampler);
	}

	return samplers;
}

shared_ptr<Core::Sampler> Core::GLTFLoader::LoadSampler(
	Device& device, tinygltf::Sampler& gltfSampler)
{
	SamplerCreateInfo samplerCreateInfo{};
	samplerCreateInfo.magFilter = FindMagFilter(gltfSampler.magFilter);
	samplerCreateInfo.minFilter = FindMinFilter(gltfSampler.minFilter);
	samplerCreateInfo.wrapS = FindWrapMode(gltfSampler.wrapS);
	samplerCreateInfo.wrapT = FindWrapMode(gltfSampler.wrapT);
	samplerCreateInfo.mipmapMode = FindMipmapMode(gltfSampler.minFilter);

	auto sampler = _resourceCache.RequestSampler(samplerCreateInfo);

	return sampler;
}

vector<shared_ptr<Core::Image>> Core::GLTFLoader::LoadImages(const string& modelPath)
{
	auto size = _model->images.size();

	vector<shared_ptr<Core::Image>> images(size);

	for (size_t i = 0; i < size; ++i)
	{
		auto image = _model->images[i];

		// From URI
		ImageCreateInfo imageCreateInfo{};
		imageCreateInfo.filePath = modelPath + "/" + image.uri;

		auto vkImage = _resourceCache.RequestImage(imageCreateInfo);
		images[i] = vkImage;
	}

	return images;
}

vector<shared_ptr<Core::Texture>> Core::GLTFLoader::LoadTextures(
	vector<shared_ptr<Core::Sampler>>& samplers, vector<shared_ptr<Core::Image>>& images)
{
	size_t size = _model->textures.size();

	vector<shared_ptr<Core::Texture>> textures(size);

	for (size_t i = 0; i < size; ++i)
	{
		int imageIndex = _model->textures[i].source;
		int samplerIndex = _model->textures[i].sampler;

		auto texture = 
			_resourceCache.RequestTexture(_model->textures[i].name,
			images[imageIndex], samplers[samplerIndex]);

		textures[i] = texture;
	}

	return textures;
}

vector<shared_ptr<Core::Material>> Core::GLTFLoader::LoadMaterials(vector<shared_ptr<Core::Texture>>& textures)
{
	size_t size = _model->materials.size();
	
	vector<shared_ptr<Core::Material>> materials(size);

	for (size_t i = 0; i < size; ++i)
	{
		auto& gltfMaterial = _model->materials[i];

		string matName = gltfMaterial.name.empty() ? 
			_modelPath + to_string(i) : gltfMaterial.name;

		//FIXME : hardcoded shader and should use lightweight pattern.
		auto material = _resourceCache.RequestMaterial(matName, "PBR");

		//Already bound
		if (material.use_count() > 1)
			continue;

		PBRBuffer* pbrBuffer = new PBRBuffer();
		material->AddBuffer(8, pbrBuffer);

		pbrBuffer->Albedo = glm::vec4(1);

		auto pbrMetallicRoughness = gltfMaterial.pbrMetallicRoughness;
		pbrBuffer->Metallic = static_cast<float>(pbrMetallicRoughness.metallicFactor);
		pbrBuffer->Roughness = static_cast<float>(pbrMetallicRoughness.roughnessFactor);

		for (auto& value : gltfMaterial.values)
		{

			if (value.first.find("baseColorFactor") != string::npos)
			{
				const auto& colorFactor = value.second.ColorFactor();
				pbrBuffer->Albedo = glm::vec4(colorFactor[0], colorFactor[1], colorFactor[2], colorFactor[3]);
			}
			else if (value.first.find("roughnessFactor") != string::npos)
			{
				pbrBuffer->Roughness = static_cast<float>(value.second.Factor());
			}
			else if (value.first.find("metallicFactor") != string::npos)
			{
				pbrBuffer->Metallic = static_cast<float>(value.second.Factor());
			}
			else if (value.first.find("baseColorTexture") != string::npos)
			{
				auto texture = textures[value.second.TextureIndex()];

				/*if (NeedSRGB(value.first))
					texture->GetImage()->SetSRGBFormat();*/

				_transferContext.Enqueue(new VkImageJob(_device, texture->GetImage(), texture->GetName()), texture->GetName());

				material->SetBuffer(3, texture);
				
				pbrBuffer->AlbedoTextureSet = 1;
			}
			else if (value.first.find("metallicRoughnessTexture") != string::npos) 
			{
				auto texture = textures[value.second.TextureIndex()];

				/*if (NeedSRGB(value.first))
					texture->GetImage()->SetSRGBFormat();*/

				_transferContext.Enqueue(new VkImageJob(_device, texture->GetImage(), texture->GetName()), texture->GetName());

				material->SetBuffer(5, texture);
				
				pbrBuffer->RoughnessTextureSet = 1;
				pbrBuffer->MetallicTextureSet = 1;
				pbrBuffer->OcclusionTextureSet = 1;
			}
		}

		for (auto& additionalValue : gltfMaterial.additionalValues)
		{
			if (additionalValue.first.find("normalTexture") != string::npos)
			{
				auto texture = textures[additionalValue.second.TextureIndex()];

				/*if (NeedSRGB(additionalValue.first))
					texture->GetImage()->SetSRGBFormat();*/

				_transferContext.Enqueue(new VkImageJob(_device, texture->GetImage(), texture->GetName()), texture->GetName());

				material->SetBuffer(4, texture);
			}
			else if (additionalValue.first.find("emissiveTexture") != string::npos)
			{
			}
			else if (additionalValue.first.find("occlusionTexture") != string::npos)
			{
			}
			else if (additionalValue.first.find("alphaMode") != string::npos)
			{
				/*tinygltf::Parameter param = additionalValue.first["alphaMode"];
				if (param.string_value == "BLEND") {
					material.alphaMode = AlphaMode::Blend;
				}
				if (param.string_value == "MASK") {
					material.alphaCutoff = 0.5f;
					material.alphaMode = AlphaMode::Mask;
				}*/
			}
			else if (additionalValue.first.find("alphaCutoff") != string::npos)
			{
				/*material.alphaCutoff =
					static_cast<float>(additionalValue.first["alphaCutoff"].Factor());*/
			}
			else if (additionalValue.first.find("emissiveFactor") != string::npos)
			{
				/*material.emissiveFactor = glm::vec4(glm::make_vec3(gltfMaterial.additionalValues["emissiveFactor"].ColorFactor().data()), 1.0);*/
			}
		}

		materials[i] = material;
	}

	return materials;
}

void Core::GLTFLoader::LoadMeshes(vector<shared_ptr<Core::Material>>& materials)
{
	size_t size = _model->meshes.size();

	vector<unique_ptr<Core::Mesh>> meshes(size);

	for (auto& gltfMesh : _model->meshes)
	{
		auto meshName = gltfMesh.name;

		unique_ptr<Core::Mesh> mesh = make_unique<Mesh>(_device);

		size_t primSize = gltfMesh.primitives.size();
		for (int i = 0; i < primSize; ++i)
		{
			string subMeshName = meshName + to_string(i);

			auto subMesh = 
				_resourceCache.RequestSubMesh(subMeshName);

			//Already jobified
			if (subMesh.use_count() > 1)
				continue;

			auto primitive = gltfMesh.primitives[i];

			size_t count = 0;
			for (auto& attribute : primitive.attributes)
			{
				string name = attribute.first;

				auto vertexData = GetAttributeData(_model, attribute.second);

				auto& accessor = _model->accessors[attribute.second];
				
				count = accessor.count;

				VkFormat format = GetAttributeFormat(_model, attribute.second);
				uint32_t stride = Utility::ToU32(GetAttributeStride(_model, attribute.second));

				_transferContext.Enqueue(new VkBufferJob(
					_device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 
					subMesh->InsertBufferSpace(name), move(vertexData)), subMeshName + name);
			}

			if (primitive.indices >= 0)
			{
				subMesh->SetIndexCount(Utility::ToU32(_model->accessors[primitive.indices].count));
				
				auto indexData = GetAttributeData(_model, primitive.indices);
				
				VkFormat format = GetAttributeFormat(_model, primitive.indices);

				VkIndexType indexType = VK_INDEX_TYPE_UINT16;

				switch (format)
				{
				case VK_FORMAT_R8_UINT:
					// Converts uint8 data into uint16 data, still represented by a uint8 vector
					indexData = ConvertDataStride(indexData, 1, 2);
					indexType = VK_INDEX_TYPE_UINT16;
					break;
				case VK_FORMAT_R16_UINT:
					indexType = VK_INDEX_TYPE_UINT16;
					break;
				case VK_FORMAT_R32_UINT:
					indexType = VK_INDEX_TYPE_UINT32;
					break;
				}

				_transferContext.Enqueue(new VkBufferJob(
					_device, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
					subMesh->InsertBufferSpace(indexType), move(indexData)), subMesh->GetName() + " index");
			}

			mesh->AddSubMesh(subMesh);
			mesh->AddMaterial(materials[primitive.material]);
		}

		_meshes.push_back(mesh.get());
		_scene.AddComponent(move(mesh));
	}
}

void Core::GLTFLoader::LoadCameras()
{
	for (auto& gltfCamera : _model->cameras)
	{
		assert(gltfCamera.type == "perspective");

		string name = gltfCamera.name;

		auto camera = make_unique<PerspectiveCamera>();
		
		camera->SetAspectRatio(static_cast<float>(gltfCamera.perspective.aspectRatio));
		camera->SetFieldOfView(static_cast<float>(gltfCamera.perspective.yfov));
		camera->SetNearPlane(static_cast<float>(gltfCamera.perspective.znear));
		camera->SetFarPlane(static_cast<float>(gltfCamera.perspective.zfar));

		_cameras.push_back(camera.get());
		_scene.AddComponent(move(camera));
	}
}

void Core::GLTFLoader::LoadNodes()
{
	auto meshes = _meshes;
	auto cameras = _cameras;
	auto lights = _lights;

	for (size_t i = 0; i < _model->nodes.size(); ++i)
	{
		auto gltfNode = _model->nodes[i];

		auto entity = make_unique<Entity>(i, gltfNode.name);

		auto& transform = entity->GetTransform();

		if (!gltfNode.translation.empty())
		{
			vec3 translation;

			std::transform(gltfNode.translation.begin(), gltfNode.translation.end(),
				&translation.x, TypeCast<double, float>{});

			transform.SetTranslation(translation);
		}

		if (!gltfNode.rotation.empty())
		{
			glm::quat rotation;

			std::transform(gltfNode.rotation.begin(), gltfNode.rotation.end(),
				&rotation.x, TypeCast<double, float>{});

			transform.SetRotation(rotation);
		}

		if (!gltfNode.scale.empty())
		{
			glm::vec3 scale;

			std::transform(gltfNode.scale.begin(), gltfNode.scale.end(),
				&scale.x, TypeCast<double, float>{});

			transform.SetScale(scale);
		}

		if (!gltfNode.matrix.empty())
		{
			glm::mat4 matrix;

			std::transform(gltfNode.matrix.begin(), gltfNode.matrix.end(), &matrix[0].x, TypeCast<double, float>{});

			transform.SetMatrix(matrix);
		}

		if (gltfNode.mesh >= 0)
		{
			auto mesh = meshes[gltfNode.mesh];

			entity->SetComponent(*mesh);
			mesh->SetEntity(entity.get());
		}

		if (gltfNode.camera >= 0)
		{
			auto camera = cameras[gltfNode.camera];

			entity->SetComponent(*camera);
			camera->SetEntity(entity.get());
		}

		if (gltfNode.light >= 0)
		{
			auto light = lights[gltfNode.light];

			entity->SetComponent(*light);
			light->SetEntity(entity.get());
		}

		_scene.AddEntity(std::move(entity));
	}
}

void Core::GLTFLoader::ClearCaches()
{
	_meshes.clear();
	_cameras.clear();
	_lights.clear();
}