#include "stdafx.h"
#include "GLTFLoader.h"
#include "Log.h"

#include "Core/VulkanWrapper/Image.h"

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

	auto samplers = LoadSamplers(model);

	auto images = LoadImages(device, model, modelPath);

	//todo : load textures?

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

vector<VkSamplerCreateInfo> Core::GLTFLoader::LoadSamplers(const tinygltf::Model& model)
{
	size_t size = model.samplers.size();

	vector<VkSamplerCreateInfo> samplers(size);

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

		samplers[i] = samplerInfo;
	}

	return samplers;
}

vector<unique_ptr<Core::Image>> Core::GLTFLoader::LoadImages(Device& device, const tinygltf::Model& model, const string& modelPath)
{
	auto size = model.images.size();

	vector<unique_ptr<Core::Image>> images(size);

	for (size_t i = 0; i < size; ++i)
	{
		auto image = model.images[i];

		unique_ptr<Core::Image> vkImage;

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
			vkImage = make_unique<Core::Image>(device, imagePath);
		}

		images[i] = move(vkImage);
	}

	return images;
}
