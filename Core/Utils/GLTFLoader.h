#pragma once
#include "Graphics/ResourceHandle.h"
#include "Graphics/GeometryUpload.h"

#define KHR_LIGHTS_PUNCTUAL_EXTENSION "KHR_lights_punctual"

namespace tinygltf
{
	class Model;
	struct Sampler;
	struct Primitive;
}

namespace Core
{
	class Scene;
	class Sampler;
	class Texture;
	class Material;
	class Mesh;
	class PerspectiveCamera;
	class Light;
	class ResourceManager;
	class RenderContext;

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

		void LoadScene(string path);
		void LoadSkybox(string path);

		void SetRenderContext(RenderContext* renderContext) { _renderContext = renderContext; }

	private:
		bool LoadFromFile(tinygltf::Model* model, const string& path);
		void LoadAssets(const string& modelPath);
		void CheckExtensions();
		void LoadLights();
		vector<Handle<Core::Sampler>> LoadSamplers();
		// Each Texture owns its own Image (built from the glTF image URI), so there
		// is no shared image list — textures are created directly from image paths.
		vector<Handle<Core::Texture>> LoadTextures(
			vector<Handle<Core::Sampler>>& samplers,
			const string& modelPath);
		vector<Handle<Core::Material>> LoadMaterials(vector<Handle<Core::Texture>>& textures);
		// Where a mesh's geometry is stored: the shared global buffers (GPU-driven draw
		// set) or buffers of its own.
		enum class GeometryStorage { Global, Standalone };

		// Reads a primitive's attributes + indices into the form ResourceManager takes.
		SubMeshGeometry ReadGeometry(const tinygltf::Primitive& primitive);
		// Scene geometry: handed to the render side's global mesh buffers.
		void LoadMeshes(vector<Handle<Core::Material>>& materials);
		// Skybox geometry: per-submesh buffers, outside the GPU-driven draw set.
		void LoadSkyboxMeshes(vector<Handle<Core::Material>>& materials);
		void LoadMeshes(vector<Handle<Core::Material>>& materials, GeometryStorage storage);
		void LoadCameras();
		void LoadNodes();
		void ClearCaches();
		Handle<Core::Sampler> LoadSampler(Device& device, tinygltf::Sampler& sampler);
	private:
		Device& _device;
		Scene& _scene;
		ResourceManager& _resourceManager;
		RenderContext* _renderContext = nullptr;

		string _modelPath;
		tinygltf::Model* _model;
		vector<Core::Mesh*> _meshes;
		vector<Core::PerspectiveCamera*> _cameras;
		vector<Core::Light*> _lights;
	};
}