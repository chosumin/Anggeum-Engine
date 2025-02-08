#pragma once

namespace tinygltf
{
	class Model;
}

namespace Core
{
	class Image;
	class GLTFLoader
	{
	public:
		static void LoadScene(Device& device, const string& path);
		static void LoadModel(const string& path);
	private:
		static void LoadScene(Device& device, const tinygltf::Model & model, const string& modelPath);
		static void CheckExtensions(const tinygltf::Model& model);
		static void LoadLights(const tinygltf::Model& model);
		static vector<VkSamplerCreateInfo> LoadSamplers(const tinygltf::Model& model);
		static vector<unique_ptr<Core::Image>> LoadImages(Device& device, const tinygltf::Model& model, const string& modelPath);
	};
}