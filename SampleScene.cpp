#include "stdafx.h"
#include "SampleScene.h"
#include "Entity.h"
#include "Component.h"
#include "PerspectiveCamera.h"
#include "FreeCamera.h"
#include "MeshRenderer.h"
#include "MaterialFactory.h"
using namespace Core;

SampleScene::SampleScene(Core::Device& device, 
	float width, float height)
{
	auto cameraEntity = make_unique<Entity>(-1, "main camera");
	auto perspectiveCamera = make_unique<PerspectiveCamera>(*cameraEntity, width, height);
	AddComponent(move(perspectiveCamera), *cameraEntity);
	auto freeCamera = make_unique<FreeCamera>(*cameraEntity);
	AddComponent(move(freeCamera), *cameraEntity);

	AddEntity(move(cameraEntity));

	//for (int i = 0; i < 500; ++i)
	{
		auto meshEntity = make_unique<Entity>(-1, "mesh");
		auto& transform = meshEntity->GetTransform();
		vec3 position = vec3(0, 0, 0);
		transform.SetTranslation(position);
		auto meshRenderer = make_unique<MeshRenderer>(device, *meshEntity, "Models/viking_room.obj");
		AddComponent(move(meshRenderer), *meshEntity);
		AddEntity(move(meshEntity));
	}

	//add ground
	auto ground = make_unique<Entity>(-1, "ground");
	auto meshRenderer = make_unique<MeshRenderer>(device, *ground, 0);
	meshRenderer->SetMaterial(Core::MaterialType::BASE);
	AddComponent(move(meshRenderer), *ground);
	AddEntity(move(ground));
}

SampleScene::~SampleScene()
{
}
