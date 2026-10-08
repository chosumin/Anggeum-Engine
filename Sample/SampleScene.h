#pragma once
#include "Foundation/Scene.h"
#include "Core/Utils/GLTFLoader.h"
#include "Utils/InputEvents.h"

namespace Core
{
	class RenderContext;
}

namespace Core { class ResourceManager; }

class SampleScene : public Core::Scene
{
public:
	SampleScene(Core::Device& device, Core::ResourceManager& resourceManager);
	~SampleScene();

	void Load(float width, float height, Core::RenderContext* renderContext,
		Core::GLTFLoader& gltfLoader);

	virtual void Update() override;
private:
	// Asset streaming test: 1 / 3 load one / ten helmets at random positions,
	// 2 / 4 unload one / ten, last loaded first.
	void UpdateStreamingTest();
	void SpawnAssets(int count);
	void OnAssetLoaded(const Core::LoadedAsset& asset);
	void DespawnAssets(int count);
	bool KeyPressedThisFrame(Core::KeyCode key);

	Core::GLTFLoader* _gltfLoader = nullptr;
	Core::RenderContext* _renderContext;
	Core::Device& _device;
	Core::ResourceManager& _resourceManager;

private:
	// The LIFO stack of requested helmets.
	std::vector<Core::AssetId> _spawned;

	std::unordered_map<Core::KeyCode, bool> _keyWasDown;
	std::mt19937 _spawnRng{ std::random_device{}() };

private:
	glm::vec3 _dirLightEuler{ 45.0f, 45.0f, 0.0f };

	std::vector<glm::vec3> _pointLightCenters;
	std::vector<glm::vec3> _spotLightCenters;
	std::vector<float> _pointLightAngles;
	std::vector<float> _spotLightAngles;
	std::vector<float> _pointLightSpeeds;
	std::vector<float> _spotLightSpeeds;
	std::vector<float> _pointLightRadii;
	std::vector<float> _spotLightRadii;
};

