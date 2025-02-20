#include "stdafx.h"
#include "GLTFLoader.h"
#include "Log.h"

#include "Core/VulkanWrapper/Image.h"
#include "Core/VulkanWrapper/Sampler.h"
#include "Core/VulkanWrapper/Texture.h"
#include "Core/Material.h"
#include "Core/Utils/Utility.h"
#include "Core/Components/Mesh.h"
#include "Core/SubMesh.h"
#include "Core/Entity.h"

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

void Core::GLTFLoader::LoadScene(Device& device, const string& path)
{
	string err;
	string warn;

	tinygltf::TinyGLTF loader;

	tinygltf::Model model;

	bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, path);

	if (ret == false)
	{
		LOG("Failed to load gltf file {}.", path);
	}

	if (err.empty() == false)
	{
		LOG("Error loading gltf file {}.", path);
	}

	if (warn.empty() == false)
	{
		LOG("Warning loading gltf file {}.", path);
	}

	size_t pos = path.find_last_of('/');
	string modelPath = path.substr(0, pos);

	LoadScene(device, model, modelPath);
}

void Core::GLTFLoader::LoadModel(const string& path)
{
	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
	string err;
	string warn;

	bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, path);

	int a = 10;
}

void Core::GLTFLoader::LoadScene(Device& device, const tinygltf::Model& model, const string& modelPath)
{
	CheckExtensions(model);

	LoadLights(model);

	auto samplers = LoadSamplers(device, model);

	auto images = LoadImages(device, model, modelPath);

	auto textures = LoadTextures(device, model, samplers, images);

	auto materials = LoadMaterials(device, model, textures);
	
	auto meshes = LoadMeshes(device, model, materials);

	//todo : load cameras

	//todo : load nodes

	//todo : load animations

	//todo : load scenes

	int a = 10;
}

void Core::GLTFLoader::CheckExtensions(const tinygltf::Model& model)
{
	for (auto& extention : model.extensionsUsed)
	{

	}
}

void Core::GLTFLoader::LoadLights(const tinygltf::Model& model)
{
}

vector<Core::Sampler*> Core::GLTFLoader::LoadSamplers(Device& device, const tinygltf::Model& model)
{
	size_t size = model.samplers.size();

	vector<Core::Sampler*> samplers(size);

	for (size_t i = 0; i < size; ++i)
	{
		auto sampler = model.samplers[i];
		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

		samplerInfo.minFilter = FindMinFilter(sampler.minFilter);
		samplerInfo.magFilter = FindMagFilter(sampler.magFilter);

		samplerInfo.addressModeU = FindWrapMode(sampler.wrapS);
		samplerInfo.addressModeV = FindWrapMode(sampler.wrapT);
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

		samplerInfo.mipmapMode = FindMipmapMode(sampler.minFilter);
		
		VkPhysicalDeviceProperties properties{};
		vkGetPhysicalDeviceProperties(device.GetPhysicalDevice(), &properties);

		samplerInfo.anisotropyEnable = VK_TRUE;
		//lower value results in better performance, but lower quality results.
		samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;

		samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;
		samplerInfo.compareEnable = VK_FALSE; //usually used for percentage-closer filtering on shadow maps.
		samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.minLod = 0.0f;

		//HACK : hardcoded.
		samplerInfo.maxLod = numeric_limits<float>::max();

		samplers[i] = new Sampler(device, samplerInfo);
	}

	return samplers;
}

vector<Core::Image*> Core::GLTFLoader::LoadImages(Device& device, const tinygltf::Model& model, const string& modelPath)
{
	auto size = model.images.size();

	vector<Core::Image*> images(size);

	for (size_t i = 0; i < size; ++i)
	{
		auto image = model.images[i];

		Core::Image* vkImage;

		//Embedded data is corrupt so use uri instead.
		
		//if (image.image.empty() == false)
		//{
		//	// Embedded
		//	vkImage = make_unique<Core::Image>(device, move(image.image));
		//}
		//else
		{
			// From URI
			auto imagePath = modelPath + "/" + image.uri;
			vkImage = new Core::Image(device, imagePath);
		}

		images[i] = move(vkImage);
	}

	return images;
}

vector<Core::Texture*> Core::GLTFLoader::LoadTextures(Device& device, 
	const tinygltf::Model& model,
	vector<Core::Sampler*>& samplers, vector<Core::Image*>& images)
{
	size_t size = model.textures.size();

	vector<Core::Texture*> textures(size);

	for (size_t i = 0; i < size; ++i)
	{
		int imageIndex = model.textures[i].source;
		int samplerIndex = model.textures[i].sampler;

		//TODO : default sampler
		auto texture = new Texture(device, model.textures[i].name,
			images[imageIndex], samplers[samplerIndex]);

		textures[i] = texture;
	}

	return textures;
}

vector<Core::Material*> Core::GLTFLoader::LoadMaterials(Device& device, 
	const tinygltf::Model& model, vector<Core::Texture*>& textures)
{
	size_t size = model.materials.size();
	
	vector<Material*> materials(size);

	for (size_t i = 0; i < size; ++i)
	{
		auto& gltfMaterial = model.materials[i];

		uint32_t hash = Utility::HashCode(gltfMaterial.name.c_str());

		//FIXME : hardcoded shader and should use lightweight pattern.
		auto material = new Material(device, "Sample", hash);

		for (auto& value : gltfMaterial.values)
		{
			if (value.first.find("Texture") != string::npos)
			{
				//Texture
				string texName = value.first;
				
				int index = value.second.TextureIndex();
				auto texture = textures[index];

				if (NeedSRGB(value.first))
					texture->GetImage()->SetSRGBFormat();

				material->SetBuffer(1, texture);
			}
		}

		for (auto& value : gltfMaterial.additionalValues)
		{
			if (value.first.find("Texture") != std::string::npos)
			{
				string texName = value.first;

				auto texture = textures[value.second.TextureIndex()];

				if (NeedSRGB(value.first))
					texture->GetImage()->SetSRGBFormat();

				//material->SetBuffer(1, texture);
			}
		}
		
		//TODO : map properties.

		materials[i] = material;
	}

	return materials;
}

vector<Core::Mesh*> Core::GLTFLoader::LoadMeshes(Device& device, const tinygltf::Model& model, vector<Core::Material*>& materials)
{
	size_t size = model.meshes.size();

	vector<Core::Mesh*> meshes(size);

	for (auto& gltfMesh : model.meshes)
	{
		auto meshName = gltfMesh.name;

		auto meshEntity = make_unique<Entity>(-1, "mesh");
		Mesh* mesh = new Mesh(*meshEntity, device);

		size_t primSize = gltfMesh.primitives.size();
		for (int i = 0; i < primSize; ++i)
		{
			string subMeshName = meshName + to_string(i);
			auto subMesh = new SubMesh(device, subMeshName);

			auto primitive = gltfMesh.primitives[i];
			for (auto& attribute : primitive.attributes)
			{
				string name = attribute.first;

				auto vertexData = GetAttributeData(&model, attribute.second);

				subMesh->CreateVertexBuffer(name, vertexData);
			}

			if (primitive.indices >= 0)
			{
				subMesh->SetIndexCount(Utility::ToU32(model.accessors[primitive.indices].count));
				
				auto indexData = GetAttributeData(&model, primitive.indices);
				
				VkFormat format = GetAttributeFormat(&model, primitive.indices);

				if (format == VK_FORMAT_R8_UINT)
				{
					// Converts uint8 data into uint16 data, still represented by a uint8 vector
					indexData = ConvertDataStride(indexData, 1, 2);
				}

				subMesh->CreateIndexBuffer(indexData);
			}

			mesh->AddSubMesh(subMesh);
			mesh->AddMaterial(materials[primitive.material]);
		}
	}

	return meshes;
}
