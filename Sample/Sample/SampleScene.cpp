#include "stdafx.h"
#include "SampleScene.h"
#include "Entity.h"
#include "Component.h"
#include "Components/PerspectiveCamera.h"
#include "Components/FreeCamera.h"
#include "Components/Light.h"
#include "Components/Transform.h"
#include "Core/Utils/GLTFLoader.h"
#include "Components/Mesh.h"
#include "Core/Material.h"
#include "Core/BufferObjects/BufferObjects.h"
using namespace Core;

SampleScene::SampleScene(Core::Device& device, float width, float height)
{
	_gltfLoader = make_unique<GLTFLoader>(device, *this);
	
	string path = "./Assets/Models/bull_head_4k.gltf/bull_head_4k.gltf";
	_gltfLoader->LoadScene(path);

	string skyTexture = "./Assets/Textures/cubemap_yokohama_rgba.ktx";
	_gltfLoader->LoadSkybox(skyTexture);

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

		auto& transform = lightEntity->GetTransform();
		transform.SetRotation(vec3(45, 45, 0));

		mainLight = light.get();
		mainLight->SetEntity(lightEntity.get());
		AddComponent(move(light), *lightEntity);
		AddEntity(move(lightEntity));
	}
}

SampleScene::~SampleScene()
{
}

void SampleScene::Update()
{
	ImGui::Begin("Mesh");
	{
		auto mesh = GetComponents<Mesh>()[0];

		auto pbr = (PBRBuffer*)mesh->GetMaterials()[0]->GetBuffer(6);

		ImGui::SliderFloat4("Albedo", &pbr->Albedo[0], 0, 1);
		ImGui::SliderFloat("Metallic", &pbr->Metallic, 0, 1);
		ImGui::SliderFloat("Roughness", &pbr->Roughness, 0, 1);
		ImGui::SliderFloat("AO", &pbr->AO, 0, 1);
		ImGui::SliderInt("Debug", &pbr->DebugMode, 0, 3);

		auto& transform = mesh->GetEntity().GetTransform();
		auto translation = transform.GetTranslation();
		ImGui::InputFloat3("Position", &translation[0]);

		transform.SetTranslation(translation);
	}
	ImGui::End();

	ImGui::Begin("Main Camera");
	{
		auto mainCamera = GetMainCamera();
		auto& transform = mainCamera->GetEntity().GetTransform();
		auto translation = transform.GetTranslation();
		ImGui::InputFloat3("Position", &translation[0]);

		auto& rotation = transform.GetRotation();
		glm::vec3 euler = glm::eulerAngles(rotation);
		euler = glm::degrees(euler);

		ImGui::InputFloat3("Direction", &euler[0]);

		transform.SetTranslation(translation);
		transform.SetRotation(euler);
	}
	ImGui::End();
}
