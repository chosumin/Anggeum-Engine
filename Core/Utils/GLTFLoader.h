#pragma once
#include "Graphics/ResourceHandle.h"
#include "Graphics/GeometryUpload.h"
#include "Foundation/Threadable.h"

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
	class Entity;
	class Sampler;
	class Texture;
	class Material;
	class Mesh;
	class PerspectiveCamera;
	class Light;
	class ResourceManager;
	class RenderContext;
	class AssetStreamer;
	class GltfParseJob;

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

	struct LoadedAsset
	{
		vector<Entity*> entities;
		vector<Handle<Core::Texture>> textures;
		vector<Handle<Core::Material>> materials;
		vector<Handle<Core::SubMesh>> subMeshes;
	};

	using AssetId = uint32_t;

	class GLTFLoader : private Threadable
	{
		friend class GltfParseJob;
	public:
		static constexpr size_t PARSE_THREADS = 2;

		GLTFLoader(Device& device, ResourceManager& resourceManager, Scene& scene,
			AssetStreamer& assetStreamer, SyncContext& syncContext);
		~GLTFLoader();

		AssetId LoadScene(string path, function<void(const LoadedAsset&)> onLoaded = nullptr);
		void UnloadScene(AssetId id);

		// Registers the parses that finished.
		void Update();

		void LoadSkybox(string path);

		void SetRenderContext(RenderContext* renderContext) { _renderContext = renderContext; }

	private:
		static bool LoadFromFile(tinygltf::Model* model, const string& path);
		void LoadAssets(const string& modelPath);
		LoadedAsset FinalizeLoad(GltfParseJob& job);
		void ReleasePendingUnloads();
		void UnloadResources(const LoadedAsset& asset);
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
		static SubMeshGeometry ReadGeometry(const tinygltf::Model* model,
			const tinygltf::Primitive& primitive);
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
		AssetStreamer& _assetStreamer;
		RenderContext* _renderContext = nullptr;

		string _modelPath;
		tinygltf::Model* _model;
		vector<Core::Mesh*> _meshes;
		vector<Core::PerspectiveCamera*> _cameras;
		vector<Core::Light*> _lights;
		LoadedAsset _loaded;

		// The parse job's output being registered: geometry in mesh/primitive
		// order (LoadMeshes) and upload sizes per glTF image (LoadTextures).
		vector<SubMeshGeometry> _pendingGeometry;
		vector<VkDeviceSize> _pendingImageBytes;

		// Parses in request order; a cancelled one is dropped when it finishes.
		struct PendingLoad
		{
			AssetId id;
			unique_ptr<GltfParseJob> job;
			function<void(const LoadedAsset&)> onLoaded;
			bool cancelled = false;
		};
		deque<PendingLoad> _pendingLoads;

		// Assets in the scene, by id.
		AssetId _nextAssetId = 1;
		unordered_map<AssetId, LoadedAsset> _assets;

		// Taken out of the scene; their resources wait for the frame's render
		// sync to release the draws and for their uploads to land.
		struct PendingUnload
		{
			LoadedAsset asset;
			u64 removedFrame;
		};
		vector<PendingUnload> _pendingUnloads;
	};
}