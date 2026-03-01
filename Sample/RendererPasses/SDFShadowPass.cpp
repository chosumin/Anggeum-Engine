#include "stdafx.h"
#include "SDFShadowPass.h"
#include "Foundation/Scene.h"
#include "Components/Light.h"
#include "Components/Mesh.h"
#include "Components/PerspectiveCamera.h"
#include "Graphics/SDFGenerator.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/ResourceCache.h"

using namespace Core;

SDFShadowPass::SDFShadowPass(Device& device, WorkerThreadManager& workerThreadManager,
	Scene& scene, VkExtent2D screenExtent)
	: RendererPass(device, workerThreadManager)
	, _scene(scene)
	, _screenExtent(screenExtent)
{
	_sdfGenerator = make_unique<SDFGenerator>(device);

	// Load compute shader for SDF shadow ray marching
	_sdfShadowShader = _device.GetResourceCache().RequestShader("sdfShadow");

	PipelineState computeState;
	_sdfShadowPipeline = make_unique<Pipeline>(_device, *_sdfShadowShader, computeState);
}

SDFShadowPass::~SDFShadowPass()
{
}

void SDFShadowPass::GenerateSDFVolume(CommandBuffer& commandBuffer)
{
	commandBuffer.BeginDebugMarker("SDF Volume Generation");

	auto meshes = _scene.GetComponents<Mesh>();

	// Filter out skybox or non-shadow-casting meshes if needed
	std::vector<Mesh*> shadowCasters;
	for (auto* mesh : meshes)
	{
		auto material = mesh->GetMaterials()[0];
		auto& shader = material->GetShader();
		if (shader.GetPass() != "Skybox")
		{
			shadowCasters.push_back(mesh);
		}
	}

	_sdfVolumeTexture = _sdfGenerator->Generate(shadowCasters, SDF_VOLUME_DIM);

	// Initialize SDF params from generated bounds
	auto boundsMin = _sdfGenerator->GetBoundsMin();
	auto boundsMax = _sdfGenerator->GetBoundsMax();

	_sdfParams.VolumeMin = vec4(boundsMin, 0.0f);
	_sdfParams.VolumeMax = vec4(boundsMax, 0.0f);
	_sdfParams.VolumeResolution = vec4(
		static_cast<float>(SDF_VOLUME_DIM),
		static_cast<float>(SDF_VOLUME_DIM),
		static_cast<float>(SDF_VOLUME_DIM),
		_maxDistance);
	_sdfParams.MaxSteps = _maxSteps;
	_sdfParams.MinDistance = _minDistance;
	_sdfParams.MaxDistance = _maxDistance;
	_sdfParams.ShadowSoftness = _shadowSoftness;

	_sdfGenerated = true;

	commandBuffer.EndDebugMarker();
}

void SDFShadowPass::EnsureRenderTargets(RenderFrame& renderFrame)
{
	// Half-res shadow map for performance
	RenderTargetDesc sdfShadowDesc{};
	sdfShadowDesc.extent = {
		_screenExtent.width / 2,
		_screenExtent.height / 2
	};
	sdfShadowDesc.format = VK_FORMAT_R8_UNORM;
	sdfShadowDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	sdfShadowDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	sdfShadowDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;

	_sdfShadowTexture = renderFrame.GetOrCreateRenderTarget(RT_SDF_SHADOW, sdfShadowDesc);
}

void SDFShadowPass::UpdateSDFParams()
{
	// Update light direction from scene
	auto lights = _scene.GetComponents<Light>();
	if (!lights.empty())
	{
		auto light = lights[0];
		auto& transform = light->GetEntity().GetTransform();
		glm::mat4 lightMatrix = transform.GetMatrix();
		glm::vec3 lightDir = glm::normalize(glm::vec3(lightMatrix[2]));
		_sdfParams.LightDirection = vec4(lightDir, _shadowSoftness);
	}

	_sdfParams.MaxSteps = _maxSteps;
	_sdfParams.MinDistance = _minDistance;
	_sdfParams.MaxDistance = _maxDistance;
	_sdfParams.ShadowSoftness = _shadowSoftness;
}

void SDFShadowPass::UpdateGUI()
{
	ImGui::Begin("SDF Shadows");

	ImGui::SliderFloat("Shadow Softness", &_shadowSoftness, 1.0f, 32.0f);
	ImGui::SliderFloat("Min Distance", &_minDistance, 0.0001f, 0.1f, "%.4f");
	ImGui::SliderFloat("Max Distance", &_maxDistance, 10.0f, 500.0f);
	ImGui::SliderInt("Max Steps", &_maxSteps, 8, 128);

	if (ImGui::Button("Regenerate SDF"))
	{
		_sdfGenerated = false;
	}

	ImGui::End();
}

void SDFShadowPass::Prepare()
{
}

void SDFShadowPass::Draw(RenderFrame& renderFrame, uint32_t imageIndex)
{
	auto& commandBuffer = renderFrame.GetCommandBuffer();

	// Generate SDF volume once (or when regeneration is requested)
	if (!_sdfGenerated)
	{
		GenerateSDFVolume(commandBuffer);
	}

	if (!_sdfVolumeTexture)
		return;

	EnsureRenderTargets(renderFrame);
	UpdateSDFParams();

	auto depthTexture = renderFrame.GetRenderTarget("MainDepth");
	if (!depthTexture)
		return;

	// Transition SDF shadow output to general for compute write
	commandBuffer.TransitionImageLayout(*_sdfShadowTexture->GetImage().lock(),
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_GENERAL);

	// Build descriptor set
	auto builder = renderFrame.CreateDescriptorSetBuilder(*_sdfShadowShader, 0);

	PerspectiveCamera* camera = _scene.GetMainCamera();
	builder.SetUniformBuffer(0, &camera->Matrices);

	builder.SetTextureBuffer(1, _sdfVolumeTexture);
	builder.SetUniformBuffer(2, &_sdfParams);

	builder.SetTextureBuffer(3, depthTexture);
	builder.SetTextureBuffer(4, _sdfShadowTexture, 0, VK_IMAGE_LAYOUT_GENERAL);
	auto& resources = builder.Build();

	commandBuffer.BindPipeline(_sdfShadowPipeline.get());
	commandBuffer.BindDescriptorSet(renderFrame,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		*_sdfShadowShader, 0, resources);

	// Dispatch compute: half-res
	uint32_t dispatchX = (_screenExtent.width / 2 + 7) / 8;
	uint32_t dispatchY = (_screenExtent.height / 2 + 7) / 8;
	commandBuffer.Dispatch(dispatchX, dispatchY, 1);

	// Transition back for sampling in fragment shader
	commandBuffer.TransitionImageLayout(*_sdfShadowTexture->GetImage().lock(),
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	UpdateGUI();
}