#include "stdafx.h"
#include "SampleScene.h"
#include "Entity.h"
#include "Component.h"
#include "Components/PerspectiveCamera.h"
#include "Components/FreeCamera.h"
#include "Components/Light.h"
#include "Core/Utils/GLTFLoader.h"
using namespace Core;

SampleScene::SampleScene(Core::Device& device, 
	float width, float height)
{
	GLTFLoader gltfLoader(device, *this);
	
	string path = "./Assets/Models/GlassHurricaneCandleHolder/glTF/GlassHurricaneCandleHolder.gltf";
	gltfLoader.LoadScene(path);

	auto mainCamera = GetMainCamera();

	if (mainCamera == nullptr)
	{
		auto cameraEntity = make_unique<Entity>(-1, "main camera");
		auto camera = make_unique<PerspectiveCamera>(width, height);

		mainCamera = camera.get();
		mainCamera->SetEntity(cameraEntity.get());
		AddComponent(move(camera), *cameraEntity);
		AddEntity(move(cameraEntity));
	}

	auto& cameraEntity = mainCamera->GetEntity();
	auto freeCamera = make_unique<FreeCamera>();
	freeCamera->SetEntity(&cameraEntity);
	AddComponent(move(freeCamera), mainCamera->GetEntity());

	auto mainLight = GetMainLight();

	if (mainLight == nullptr)
	{
		auto lightEntity = make_unique<Entity>(-1, "main light");
		auto light = make_unique<Core::Light>("main light");

		mainLight = light.get();
		mainLight->SetEntity(lightEntity.get());
		AddComponent(move(light), *lightEntity);
		AddEntity(move(lightEntity));
	}
}

SampleScene::~SampleScene()
{
}
