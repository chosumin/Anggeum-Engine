#pragma once

#define KHR_LIGHTS_PUNCTUAL_EXTENSION "KHR_lights_punctual"

namespace tinygltf
{
	class Model;
	class Sampler;
}

namespace Core
{
	class Scene;
	class Image;
	class Sampler;
	class Texture;
	class Material;
	class Mesh;

	/**
	 * @brief Helper Function to change array type T to array type Y
	 * Create a struct that can be used with std::transform so that we do not need to recreate lambda functions
	 * @param T
	 * @param Y
	 */
	template <class T, class Y>
	struct TypeCast
	{
		Y operator()(T value) const noexcept
		{
			return static_cast<Y>(value);
		}
	};

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
		Core::Sampler* LoadSampler(tinygltf::Sampler& sampler);
		vector<Core::Image*> LoadImages(const string& modelPath);
		vector<Core::Texture*> LoadTextures(
			vector<Core::Sampler*>& samplers,
			vector<Core::Image*>& images);
		void LoadDefaultTexture();
		vector<Core::Material*> LoadMaterials(vector<Core::Texture*>& textures);
		void LoadMeshes(vector<Core::Material*>& materials);
		void LoadCameras();
		void LoadNodes();
	private:
		Device& _device;
		Scene& _scene;
		tinygltf::Model* _model;
		Core::Texture* _defaultTexture;
	};
}