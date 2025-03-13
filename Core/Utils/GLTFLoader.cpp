#include "stdafx.h"
#include "GLTFLoader.h"
#include "Log.h"

#include "Scene.h"
#include "Core/VulkanWrapper/Image.h"
#include "Core/VulkanWrapper/Sampler.h"
#include "Core/VulkanWrapper/Texture.h"
#include "Core/Material.h"
#include "Core/Utils/Utility.h"
#include "Core/Components/Mesh.h"
#include "Core/SubMesh.h"
#include "Core/Entity.h"
#include "Core/VulkanWrapper/Vertex.h"
#include "Core/Components/PerspectiveCamera.h"
#include "Core/Components/FreeCamera.h"

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

Core::GLTFLoader::GLTFLoader(Device& device, Scene& scene)
	: _device(device), _scene(scene)
{
	_model = new tinygltf::Model();
}

Core::GLTFLoader::~GLTFLoader()
{
	delete(_model);
}

void Core::GLTFLoader::LoadScene(const string& path)
{
	string err;
	string warn;

	tinygltf::TinyGLTF loader;

	bool ret = loader.LoadASCIIFromFile(_model, &err, &warn, path);

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

	LoadAssets(modelPath);
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

void Core::GLTFLoader::LoadAssets(const string& modelPath)
{
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
}

vector<Core::Sampler*> Core::GLTFLoader::LoadSamplers()
{
	size_t size = _model->samplers.size();

	vector<Core::Sampler*> samplers(size);

	for (size_t i = 0; i < size; ++i)
	{
		auto sampler = _model->samplers[i];
		VkSamplerCreateInfo samplerInfo{};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

		samplerInfo.minFilter = FindMinFilter(sampler.minFilter);
		samplerInfo.magFilter = FindMagFilter(sampler.magFilter);

		samplerInfo.addressModeU = FindWrapMode(sampler.wrapS);
		samplerInfo.addressModeV = FindWrapMode(sampler.wrapT);
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

		samplerInfo.mipmapMode = FindMipmapMode(sampler.minFilter);
		
		VkPhysicalDeviceProperties properties{};
		vkGetPhysicalDeviceProperties(_device.GetPhysicalDevice(), &properties);

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

		samplers[i] = new Sampler(_device, samplerInfo);
	}

	return samplers;
}

vector<Core::Image*> Core::GLTFLoader::LoadImages(const string& modelPath)
{
	auto size = _model->images.size();

	vector<Core::Image*> images(size);

	for (size_t i = 0; i < size; ++i)
	{
		auto image = _model->images[i];

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
			vkImage = new Core::Image(_device, imagePath);
		}

		images[i] = move(vkImage);
	}

	return images;
}

vector<Core::Texture*> Core::GLTFLoader::LoadTextures(
	vector<Core::Sampler*>& samplers, vector<Core::Image*>& images)
{
	size_t size = _model->textures.size();

	vector<Core::Texture*> textures(size);

	for (size_t i = 0; i < size; ++i)
	{
		int imageIndex = _model->textures[i].source;
		int samplerIndex = _model->textures[i].sampler;

		//TODO : default sampler
		auto texture = new Texture(_device, _model->textures[i].name,
			images[imageIndex], samplers[samplerIndex]);

		textures[i] = texture;
	}

	return textures;
}

vector<Core::Material*> Core::GLTFLoader::LoadMaterials(vector<Core::Texture*>& textures)
{
	size_t size = _model->materials.size();
	
	vector<Material*> materials(size);

	for (size_t i = 0; i < size; ++i)
	{
		auto& gltfMaterial = _model->materials[i];

		uint32_t hash = Utility::HashCode(gltfMaterial.name.c_str());

		//FIXME : hardcoded shader and should use lightweight pattern.
		auto material = new Material(_device, "Sample", hash);

		for (auto& value : gltfMaterial.values)
		{
			if (value.first.find("baseColorTexture") != string::npos)
			{
				//Texture
				string texName = value.first;
				
				int index = value.second.TextureIndex();
				auto texture = textures[index];

				if (NeedSRGB(value.first))
					texture->GetImage()->SetSRGBFormat();

				material->SetBuffer(1, texture);
			}

			//TODO : parse PBR textures
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

void Core::GLTFLoader::LoadMeshes(vector<Core::Material*>& materials)
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
			auto subMesh = new SubMesh(_device, subMeshName);

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

				subMesh->CreateVertexBuffer(name, vertexData);
			}

			//ADD VERTEX COLOR
			if (subMesh->HasVertexAttribute(VertexAttributeName::Col) == false)
			{
				vector<uint8_t> colorData;

				float color[3] = {1.0f, 1.0f, 1.0f};
				auto bytes = Core::Utility::ToBytes(color);
				for (size_t i = 0; i < count; ++i)
				{
					colorData.insert(colorData.end(), bytes.begin(), bytes.end());
				}

				subMesh->CreateVertexBuffer(VertexAttributeName::Col, colorData);
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

				subMesh->CreateIndexBuffer(indexData, indexType);
			}

			mesh->AddSubMesh(subMesh);
			mesh->AddMaterial(materials[primitive.material]);

			_scene.AddComponent(move(mesh));
		}
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

		_scene.AddComponent(move(camera));
	}
}

void Core::GLTFLoader::LoadNodes()
{
	auto meshes = _scene.GetComponents<Mesh>();
	auto cameras = _scene.GetComponents<PerspectiveCamera>();

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

		/*if (auto extension = get_extension(gltfNode.extensions, KHR_LIGHTS_PUNCTUAL_EXTENSION))
		{
			auto lights = scene.get_components<sg::Light>();
			int  light_index = extension->Get("light").Get<int>();
			assert(light_index < lights.size());
			auto light = lights[light_index];

			node->set_component(*light);

			light->set_node(*node);
		}*/

		_scene.AddEntity(std::move(entity));
	}
}
