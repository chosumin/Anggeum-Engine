#pragma once

#define KHR_LIGHTS_PUNCTUAL_EXTENSION "KHR_lights_punctual"

namespace tinygltf
{
	class Model;
	struct Sampler;
}

namespace Core
{
	class Scene;
	class Image;
	class Sampler;
	class Texture;
	class Material;
	class Mesh;
	class PerspectiveCamera;
	class CommandBuffer;
	class Light;
	class TransferContext;
	class ResourceCache;

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
		GLTFLoader(Device& device, Scene& scene, TransferContext& transferContext);
		~GLTFLoader();

		void LoadScene(string path);
		void LoadSkybox(string path);
	private:
		bool LoadFromFile(tinygltf::Model* model, const string& path);
		void LoadAssets(const string& modelPath);
		void CheckExtensions();
		void LoadLights();
		vector<shared_ptr<Core::Sampler>> LoadSamplers();
		vector<shared_ptr<Core::Image>> LoadImages(const string& modelPath);
		vector<shared_ptr<Core::Texture>> LoadTextures(
			vector<shared_ptr<Core::Sampler>>& samplers,
			vector<shared_ptr<Core::Image>>& images);
		vector<shared_ptr<Core::Material>> LoadMaterials(vector<shared_ptr<Core::Texture>>& textures);
		void LoadMeshes(vector<shared_ptr<Core::Material>>& materials);
		void LoadCameras();
		void LoadNodes();
		void ClearCaches();
		shared_ptr<Core::Sampler> LoadSampler(Device& device, tinygltf::Sampler& sampler);
	private:
		Device& _device;
		Scene& _scene;
		ResourceCache& _resourceCache;

		TransferContext& _transferContext;

		tinygltf::Model* _model;

		vector<Core::Mesh*> _meshes;
		vector<Core::PerspectiveCamera*> _cameras;
		vector<Core::Light*> _lights;
	};
}