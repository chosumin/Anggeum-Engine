#include "stdafx.h"
#include "GLTFLoader.h"
#include "Log.h"

#include "Core/VulkanWrapper/Image.h"
#include "Core/VulkanWrapper/Sampler.h"
#include "Core/VulkanWrapper/Texture.h"

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

	//todo : load textures?
	auto textures = LoadTextures(device, model, samplers, images);

	//todo : load materials

	//todo : load meshes

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
