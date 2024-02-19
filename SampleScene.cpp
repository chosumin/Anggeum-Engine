#include "stdafx.h"
#include "SampleScene.h"
#include "Entity.h"
#include "Component.h"
#include "PerspectiveCamera.h"
#include "FreeCamera.h"
#include "Mesh.h"
using namespace Core;

SampleScene::SampleScene(float width, float height, VkCommandPool commandPool)
{
	auto rootEntity = make_unique<Entity>(0, "root");
	
	SetRootEntity(*rootEntity);

	auto cameraEntity = make_unique<Entity>(-1, "main camera");
	auto perspectiveCamera = make_unique<PerspectiveCamera>(*cameraEntity, width, height);
	AddComponent(move(perspectiveCamera), *cameraEntity);
	auto freeCamera = make_unique<FreeCamera>(*cameraEntity);
	AddComponent(move(freeCamera), *cameraEntity);

	AddEntity(move(cameraEntity));

	for (int i = 0; i < 2; ++i)
	{
		auto meshEntity = make_unique<Entity>(-1, "mesh");
		auto& transform = meshEntity->GetTransform();
		vec3 position = vec3(i, 0, 0);
		transform.SetTranslation(position);
		auto mesh = make_unique<Mesh>(*meshEntity, commandPool, "Models/viking_room.obj");
		AddComponent(move(mesh), *meshEntity);
		AddEntity(move(meshEntity));
	}
}
