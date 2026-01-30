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
#include "Graphics/RenderContext.h"
using namespace Core;

SampleScene::SampleScene(Core::Device& device, float width, float height, TransferContext* transferContext, Core::RenderContext* renderContext)
	:_renderContext(renderContext)
{
	_gltfLoader = make_unique<Core::GLTFLoader>(device, *this, *transferContext);

	_gltfLoader->SetRenderContext(renderContext);

	string path = "./Assets/Models/Sponza/glTF/Sponza.gltf";
	_gltfLoader->LoadScene(path);

	//string path = "./Assets/Models/DamagedHelmet/glTF/DamagedHelmet.gltf";
	//_gltfLoader->LoadScene(path);

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
	if (_renderContext->IsGpuDrivenRenderingEnabled())
	{
		ImGui::Begin("GPU Driven Rendering Info");
		{
			auto materialManager = _renderContext->GetMaterialManager();
			ImGui::Text("Material Uniform Array Status:");
			ImGui::Separator();

			uint32_t materialCount = materialManager->GetMaterialCount();
			ImGui::Text("Registered Materials: %u / %u",
				materialCount,
				MaterialManager::MAX_MATERIALS);

			float usage = (float)materialCount / (float)MaterialManager::MAX_MATERIALS * 100.0f;
			ImGui::ProgressBar(usage / 100.0f, ImVec2(0.0f, 0.0f));
			ImGui::SameLine();
			ImGui::Text("Usage: %.1f%%", usage);

			ImGui::Separator();
			ImGui::Text("Uniform Buffer Size: %zu KB",
				sizeof(GPUMaterialData) * MaterialManager::MAX_MATERIALS / 1024);

			auto meshBufferManager = _renderContext->GetMeshBufferManager();
			ImGui::Text("MeshBufferManager initialized:");
			ImGui::Text("  Total Vertices: %u", meshBufferManager->GetTotalVertexCount());
			ImGui::Text("  Total Indices: %u", meshBufferManager->GetTotalIndexCount());
			ImGui::Text("  Allocated Meshes: %u", meshBufferManager->GetAllocatedMeshCount());
		}
		ImGui::End();
	}

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