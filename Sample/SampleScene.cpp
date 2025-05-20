#include "stdafx.h"
#include "SampleScene.h"
#include "Foundation/Entity.h"
#include "Foundation/Component.h"
#include "Foundation/WorkerThread.h"
#include "Components/PerspectiveCamera.h"
#include "Components/FreeCamera.h"
#include "Components/Light.h"
#include "Components/Transform.h"
#include "Core/Utils/GLTFLoader.h"
#include "Components/Mesh.h"
#include "Graphics/Material.h"
#include "Graphics/BufferObjects.h"
using namespace Core;

SampleScene::SampleScene(Core::Device& device, float width, float height, TransferContext* transferContext)
{
	_gltfLoader = make_unique<Core::GLTFLoader>(device, *this, *transferContext);

	string path = "./Assets/Models/DamagedHelmet/glTF/DamagedHelmet.gltf";
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

	{
		LightProperties lightProperties{};
		lightProperties.Range = 1.0f;
		lightProperties.InnerConeAngle = 10.0f;
		lightProperties.OuterConeAngle = 100.0f;

		auto lightEntity = make_unique<Entity>(-1, "point light");
		auto light = make_unique<Core::Light>("point light");
		light->SetLightType(LightType::Point);
		light->SetProperties(lightProperties);

		auto& transform = lightEntity->GetTransform();
		transform.SetRotation(vec3(45, 45, 0));

		mainLight = light.get();
		mainLight->SetEntity(lightEntity.get());
		AddComponent(move(light), *lightEntity);
		AddEntity(move(lightEntity));
	}
	{
		LightProperties lightProperties{};
		lightProperties.Range = 1.0f;
		lightProperties.InnerConeAngle = 10.0f;
		lightProperties.OuterConeAngle = 100.0f;

		auto lightEntity = make_unique<Entity>(-1, "spot light");
		auto light = make_unique<Core::Light>("spot light");
		light->SetLightType(LightType::Spot);
		light->SetProperties(lightProperties);

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

	auto lights = GetComponents<Light>();

	for (size_t i = 0; i < lights.size(); ++i)
	{
		string label;
		if (i == 0)
			label = "Directional light";
		else if (i == 1)
			label = "Point light";
		else if (i == 2)
			label = "Spot light";

		ImGui::Begin(label.c_str());

		auto& properties = lights[i]->GetProperties();
		auto& transform = lights[i]->GetEntity().GetTransform();

		auto& rotation = transform.GetRotation();
		glm::vec3 euler = glm::eulerAngles(rotation);
		euler = glm::degrees(euler);

		auto& position = transform.GetTranslation();

		ImGui::SliderFloat3("Color", &properties.Color[0], 0, 1);
		ImGui::InputFloat3("Position", &position[0]);
		transform.SetTranslation(position);
		ImGui::SliderFloat3("Direction", &euler[0], -90.0f, 90.0f);
		transform.SetRotation(euler);

		if(i != 0)
			ImGui::SliderFloat("Range", &properties.Range, 0, 10);

		if (i == 2)
		{
			ImGui::SliderFloat("Inner cone angle", &properties.InnerConeAngle, 0, 360);
			ImGui::SliderFloat("Outer cone angle", &properties.OuterConeAngle, 0, 360);
		}

		ImGui::End();
	}
}
