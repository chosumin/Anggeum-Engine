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
	:_renderContext(renderContext), _device(device)
{
	_gltfLoader = make_unique<Core::GLTFLoader>(device, *this, *transferContext);

	_gltfLoader->SetRenderContext(renderContext);

	string path = "./Assets/Models/Sponza/glTF/Sponza.gltf";
	_gltfLoader->LoadScene(path);

	//string path = "./Assets/Models/DamagedHelmet/glTF/DamagedHelmet.gltf";
	//_gltfLoader->LoadScene(path);

	string skyTexture = "./Assets/Textures/skybox.ktx";
	//string skyTexture = "./Assets/Textures/cubemap_yokohama_rgba.ktx";
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

	// Random number generator setup
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_real_distribution<float> posX(0.0f, 0.0f);
	std::uniform_real_distribution<float> posY(0.0f, 0.2f);
	std::uniform_real_distribution<float> posZ(0.0f, 0.0f);
	std::uniform_real_distribution<float> colorDist(0.3f, 1.0f);
	std::uniform_real_distribution<float> rangeDist(2.0f, 8.0f);
	std::uniform_real_distribution<float> rotDist(-180.0f, 180.0f);
	std::uniform_real_distribution<float> radiusDist(0.5f, 2.0f);
	std::uniform_real_distribution<float> speedDist(0.5f, 2.0f);
	std::uniform_real_distribution<float> angleDist(0.0f, 360.0f);

	// Reserve space for animation data
	_pointLightCenters.reserve(15);
	_pointLightAngles.reserve(15);
	_pointLightSpeeds.reserve(15);
	_pointLightRadii.reserve(15);

	// Create 15 Point Lights
	for (int i = 0; i < 15; ++i)
	{
		LightProperties lightProperties{};
		lightProperties.Color = vec3(colorDist(gen), colorDist(gen), colorDist(gen));
		lightProperties.Range = rangeDist(gen);

		auto lightEntity = make_unique<Entity>(-1, "point light " + std::to_string(i));
		auto light = make_unique<Core::Light>("point light " + std::to_string(i));
		light->SetLightType(LightType::Point);
		light->SetProperties(lightProperties);

		vec3 center(posX(gen), posY(gen), posZ(gen));
		auto& transform = lightEntity->GetTransform();
		transform.SetTranslation(center);

		// Store animation data
		_pointLightCenters.push_back(center);
		_pointLightAngles.push_back(angleDist(gen));
		_pointLightSpeeds.push_back(speedDist(gen));
		_pointLightRadii.push_back(radiusDist(gen));

		light->SetEntity(lightEntity.get());
		AddComponent(move(light), *lightEntity);
		AddEntity(move(lightEntity));
	}

	// Reserve space for spot light animation data
	_spotLightCenters.reserve(15);
	_spotLightAngles.reserve(15);
	_spotLightSpeeds.reserve(15);
	_spotLightRadii.reserve(15);

	// Create 15 Spot Lights
	for (int i = 0; i < 15; ++i)
	{
		LightProperties lightProperties{};
		lightProperties.Color = vec3(colorDist(gen), colorDist(gen), colorDist(gen));
		lightProperties.Range = rangeDist(gen);
		lightProperties.InnerConeAngle = 10.0f;
		lightProperties.OuterConeAngle = 45.0f;

		auto lightEntity = make_unique<Entity>(-1, "spot light " + std::to_string(i));
		auto light = make_unique<Core::Light>("spot light " + std::to_string(i));
		light->SetLightType(LightType::Spot);
		light->SetProperties(lightProperties);

		vec3 center(posX(gen), posY(gen), posZ(gen));
		auto& transform = lightEntity->GetTransform();
		transform.SetTranslation(center);
		transform.SetRotation(vec3(rotDist(gen), rotDist(gen), rotDist(gen)));

		// Store animation data
		_spotLightCenters.push_back(center);
		_spotLightAngles.push_back(angleDist(gen));
		_spotLightSpeeds.push_back(speedDist(gen));
		_spotLightRadii.push_back(radiusDist(gen));

		light->SetEntity(lightEntity.get());
		AddComponent(move(light), *lightEntity);
		AddEntity(move(lightEntity));
	}
}

SampleScene::~SampleScene()
{
}

void SampleScene::Update()
{
	ImGui::Begin("Scene Information");

	// GPU Driven Rendering Info Section
	{
		ImGui::SeparatorText("GPU Driven Rendering Info");

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

	// Main Camera Section
	ImGui::SeparatorText("Main Camera");
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

	// Lights Section
	ImGui::SeparatorText("Lights");
	auto lights = GetComponents<Light>();

	// Directional Light (first light)
	if (!lights.empty() && ImGui::TreeNode("Directional Light"))
	{
		auto& properties = lights[0]->GetProperties();
		auto& transform = lights[0]->GetEntity().GetTransform();

		auto& position = transform.GetTranslation();

		ImGui::SliderFloat3("Color", &properties.Color[0], 0, 1);
		ImGui::SliderFloat("Intensity", &properties.Intensity, 0.0f, 100.0f);
		ImGui::InputFloat3("Position", &position[0]);
		transform.SetTranslation(position);

		// DragFloat3: drag to rotate freely without clamping, enabling full 360�� rotation
		ImGui::DragFloat3("Rotation (Pitch / Yaw / Roll)", &_dirLightEuler[0], 1.0f);
		transform.SetRotation(_dirLightEuler);

		ImGui::TreePop();
	}

	// Animate Point Lights
	float deltaTime = ImGui::GetIO().DeltaTime * 20.0f;
	for (size_t i = 0; i < 15; ++i)
	{
		size_t lightIndex = 1 + i; // Skip directional light
		if (lightIndex < lights.size())
		{
			_pointLightAngles[i] += _pointLightSpeeds[i] * deltaTime;
			
			float angleRad = glm::radians(_pointLightAngles[i]);
			float offsetX = _pointLightRadii[i] * glm::cos(angleRad);
			float offsetZ = _pointLightRadii[i] * glm::sin(angleRad);
			
			glm::vec3 newPos = _pointLightCenters[i] + glm::vec3(offsetX, 0.0f, offsetZ);
			lights[lightIndex]->GetEntity().GetTransform().SetTranslation(newPos);
		}
	}

	// Animate Spot Lights
	for (size_t i = 0; i < 15; ++i)
	{
		size_t lightIndex = 16 + i; // Skip directional(1) + point lights(15)
		if (lightIndex < lights.size())
		{
			_spotLightAngles[i] += _spotLightSpeeds[i] * deltaTime;
			
			float angleRad = glm::radians(_spotLightAngles[i]);
			float offsetX = _spotLightRadii[i] * glm::cos(angleRad);
			float offsetZ = _spotLightRadii[i] * glm::sin(angleRad);
			
			glm::vec3 newPos = _spotLightCenters[i] + glm::vec3(offsetX, 0.0f, offsetZ);
			lights[lightIndex]->GetEntity().GetTransform().SetTranslation(newPos);
		}
	}

	// Point Lights
	{
		static int pointLightIndex = 0;
		ImGui::Text("Point Lights (Total: 15)");
		ImGui::InputInt("Point Light Index", &pointLightIndex);
		pointLightIndex = glm::clamp(pointLightIndex, 0, 14);

		if (ImGui::TreeNode("Point Light Editor"))
		{
			size_t actualIndex = 1 + pointLightIndex; // Skip directional light at index 0
			if (actualIndex < lights.size())
			{
				auto& properties = lights[actualIndex]->GetProperties();
				auto& transform = lights[actualIndex]->GetEntity().GetTransform();
				auto& position = transform.GetTranslation();

				ImGui::Text("Light Name: %s", lights[actualIndex]->GetEntity().GetName().c_str());
				ImGui::SliderFloat3("Color", &properties.Color[0], 0, 1);
				ImGui::InputFloat3("Position", &position[0]);
				ImGui::SliderFloat("Range", &properties.Range, 0, 10);
				
				ImGui::Separator();
				ImGui::Text("Animation Settings:");
				ImGui::InputFloat3("Orbit Center", &_pointLightCenters[pointLightIndex][0]);
				ImGui::SliderFloat("Orbit Radius", &_pointLightRadii[pointLightIndex], 0.1f, 5.0f);
				ImGui::SliderFloat("Orbit Speed", &_pointLightSpeeds[pointLightIndex], 0.1f, 5.0f);
			}

			ImGui::TreePop();
		}
	}

	// Spot Lights
	{
		static int spotLightIndex = 0;
		ImGui::Text("Spot Lights (Total: 15)");
		ImGui::InputInt("Spot Light Index", &spotLightIndex);
		spotLightIndex = glm::clamp(spotLightIndex, 0, 14);

		if (ImGui::TreeNode("Spot Light Editor"))
		{
			size_t actualIndex = 16 + spotLightIndex; // Skip directional(1) + point lights(15)
			if (actualIndex < lights.size())
			{
				auto& properties = lights[actualIndex]->GetProperties();
				auto& transform = lights[actualIndex]->GetEntity().GetTransform();

				auto& rotation = transform.GetRotation();
				glm::vec3 euler = glm::eulerAngles(rotation);
				euler = glm::degrees(euler);

				auto& position = transform.GetTranslation();

				ImGui::Text("Light Name: %s", lights[actualIndex]->GetEntity().GetName().c_str());
				ImGui::SliderFloat3("Color", &properties.Color[0], 0, 1);
				ImGui::InputFloat3("Position", &position[0]);
				ImGui::SliderFloat3("Direction", &euler[0], -90.0f, 90.0f);
				transform.SetRotation(euler);
				ImGui::SliderFloat("Range", &properties.Range, 0, 10);
				ImGui::SliderFloat("Inner cone angle", &properties.InnerConeAngle, 0, 360);
				ImGui::SliderFloat("Outer cone angle", &properties.OuterConeAngle, 0, 360);
				
				ImGui::Separator();
				ImGui::Text("Animation Settings:");
				ImGui::InputFloat3("Orbit Center", &_spotLightCenters[spotLightIndex][0]);
				ImGui::SliderFloat("Orbit Radius", &_spotLightRadii[spotLightIndex], 0.1f, 5.0f);
				ImGui::SliderFloat("Orbit Speed", &_spotLightSpeeds[spotLightIndex], 0.1f, 5.0f);
			}

			ImGui::TreePop();
		}
	}

	ImGui::End();
}