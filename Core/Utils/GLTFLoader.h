#pragma once

namespace tinygltf
{
	class Model;
}

namespace Core
{
	class Scene;
	class Image;
	class Sampler;
	class Texture;
	class Material;
	class Mesh;
	class GLTFLoader
	{
	public:
		GLTFLoader(Device& device, Scene& scene);
		~GLTFLoader();

		void LoadScene(const string& path);
		void LoadModel(const string& path);
	private:
		void LoadAssets(const string& modelPath);
		void CheckExtensions();
		void LoadLights();
		vector<Core::Sampler*> LoadSamplers();
		vector<Core::Image*> LoadImages(const string& modelPath);
		vector<Core::Texture*> LoadTextures(
			vector<Core::Sampler*>& samplers,
			vector<Core::Image*>& images);
		vector<Core::Material*> LoadMaterials(vector<Core::Texture*>& textures);
		void LoadMeshes(vector<Core::Material*>& materials);
		void LoadCameras();
		void LoadNodes();
	private:
		Device& _device;
		Scene& _scene;
		tinygltf::Model* _model;
	};
}