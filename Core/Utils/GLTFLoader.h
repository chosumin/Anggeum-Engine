#pragma once

namespace tinygltf
{
	class Model;
}

namespace Core
{
	class Image;
	class Sampler;
	class Texture;
	class GLTFLoader
	{
	public:
		static void LoadScene(Device& device, const string& path);
		static void LoadModel(const string& path);
	private:
		static void LoadScene(Device& device, const tinygltf::Model & model, const string& modelPath);
		static void CheckExtensions(const tinygltf::Model& model);
		static void LoadLights(const tinygltf::Model& model);
		static vector<Core::Sampler*> LoadSamplers(Device& device, const tinygltf::Model& model);
		static vector<Core::Image*> LoadImages(Device& device, const tinygltf::Model& model, const string& modelPath);
		static vector<Core::Texture*> LoadTextures(Device& device,
			const tinygltf::Model& model,
			vector<Core::Sampler*>& samplers,
			vector<Core::Image*>& images);
	};
}