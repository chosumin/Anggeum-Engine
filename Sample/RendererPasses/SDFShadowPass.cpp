#include "stdafx.h"
#include "SDFShadowPass.h"
#include "Foundation/Scene.h"
#include "Components/Light.h"
#include "Components/Mesh.h"
#include "Components/PerspectiveCamera.h"
#include "Graphics/SDFGenerator.h"
#include "Graphics/MeshBufferManager.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/ResourceCache.h"

using namespace Core;

SDFShadowPass::SDFShadowPass(Device& device, WorkerThreadManager& workerThreadManager,
	Scene& scene, VkExtent2D screenExtent,
	VkSampleCountFlagBits msaaSamples)
	: RendererPass(device, workerThreadManager)
	, _scene(scene)
	, _screenExtent(screenExtent)
	, _msaaSamples(msaaSamples)
{
	_sdfGenerator = make_unique<SDFGenerator>(device);

	_sdfShadowShader = _device.GetResourceCache().RequestShader("Shaders/sdfShadow.comp.spv");
	_sdfShadowPipeline = make_unique<Pipeline>(_device, *_sdfShadowShader);

	// Reuse the same depth resolve shader as RendererBatches (Hi-Z)
	_depthResolveShader = _device.GetResourceCache().RequestShader("Shaders/depthResolve.comp.spv");
	_depthResolvePipeline = make_unique<Pipeline>(_device, *_depthResolveShader);
}

SDFShadowPass::~SDFShadowPass()
{
}

void SDFShadowPass::EnsureRenderTargets(RenderFrame& renderFrame)
{
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

	// Resolved single-sample depth for compute shader sampling
	if (_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
	{
		RenderTargetDesc resolvedDepthDesc{};
		resolvedDepthDesc.extent = _screenExtent;
		resolvedDepthDesc.format = VK_FORMAT_R32_SFLOAT;
		resolvedDepthDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		resolvedDepthDesc.samples = VK_SAMPLE_COUNT_1_BIT;
		resolvedDepthDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;

		_resolvedDepthTexture = renderFrame.GetOrCreateRenderTarget(RT_SDF_RESOLVED_DEPTH, resolvedDepthDesc);
	}
}

void SDFShadowPass::ResolveDepth(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
	shared_ptr<Texture> msaaDepth)
{
	auto& resolvedImage = *_resolvedDepthTexture->GetImage().lock();
	commandBuffer.TransitionImageLayout(resolvedImage,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_GENERAL);

	struct DepthResolvePushConstants {
		int32_t outputWidth;
		int32_t outputHeight;
		int32_t sampleCount;
		int32_t padding;
	} resolvePc = {
		static_cast<int32_t>(_screenExtent.width),
		static_cast<int32_t>(_screenExtent.height),
		static_cast<int32_t>(_msaaSamples),
		0
	};

	auto resolveBuilder = renderFrame.CreateDescriptorSetBuilder(*_depthResolveShader, 0);
	resolveBuilder.SetTextureBuffer(0, msaaDepth);
	resolveBuilder.SetTextureBuffer(1, _resolvedDepthTexture, 0, VK_IMAGE_LAYOUT_GENERAL);
	auto& resolveResources = resolveBuilder.Build();

	commandBuffer.BindPipeline(_depthResolvePipeline.get());
	commandBuffer.BindDescriptorSet(renderFrame,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		*_depthResolveShader, 0, resolveResources);
	commandBuffer.PushConstants(*_depthResolveShader, 0, &resolvePc);

	uint32_t groupX = (_screenExtent.width + 7) / 8;
	uint32_t groupY = (_screenExtent.height + 7) / 8;
	commandBuffer.Dispatch(groupX, groupY, 1);

	commandBuffer.TransitionImageLayout(resolvedImage,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void SDFShadowPass::UpdateSDFParams()
{
	auto lights = _scene.GetComponents<Light>();
	if (!lights.empty())
	{
		auto light = lights[0];
		auto& transform = light->GetEntity().GetTransform();
		glm::mat4 lightMatrix = transform.GetMatrix();
		glm::vec3 lightDir = glm::normalize(glm::vec3(lightMatrix[2]));
		_sdfParams.LightDirection = vec4(lightDir, _shadowSoftness);
	}

	_sdfParams.VolumeMin = vec4(_sdfGenerator->GetBoundsMin(), 0.0f);
	_sdfParams.VolumeMax = vec4(_sdfGenerator->GetBoundsMax(), 0.0f);
	_sdfParams.VolumeResolution = vec4(
		static_cast<float>(SDF_VOLUME_DIM),
		static_cast<float>(SDF_VOLUME_DIM),
		static_cast<float>(SDF_VOLUME_DIM),
		_maxDistance);
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
	auto* meshBufferManager = renderFrame.GetMeshBufferManager();
	if (!meshBufferManager)
		return;

	auto& commandBuffer = renderFrame.GetCommandBuffer();

	if (!_sdfGenerated)
	{
		commandBuffer.BeginDebugMarker("SDF Volume Generation (GPU)");
		_sdfGenerator->Generate(renderFrame, commandBuffer,
			*meshBufferManager, SDF_VOLUME_DIM);
		_sdfGenerated = true;
		commandBuffer.EndDebugMarker();
	}

	auto sdfTexture = _sdfGenerator->GetSDFTexture();
	if (!sdfTexture)
		return;

	EnsureRenderTargets(renderFrame);
	UpdateSDFParams();

	auto depthTexture = renderFrame.GetRenderTarget("MainDepth");
	if (!depthTexture)
		return;

	commandBuffer.BeginDebugMarker("SDF Shadow Ray March");

	// Transition depth to SHADER_READ_ONLY for sampling
	commandBuffer.TransitionImageLayout(*depthTexture->GetImage().lock(),
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// Resolve MSAA depth to single-sample texture for compute shader
	shared_ptr<Texture> depthForSampling = depthTexture;
	if (_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
	{
		commandBuffer.BeginDebugMarker("Resolve MSAA Depth");
		ResolveDepth(renderFrame, commandBuffer, depthTexture);
		commandBuffer.EndDebugMarker();
		depthForSampling = _resolvedDepthTexture;
	}

	commandBuffer.TransitionImageLayout(*_sdfShadowTexture->GetImage().lock(),
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_GENERAL);

	auto builder = renderFrame.CreateDescriptorSetBuilder(*_sdfShadowShader, 0);

	PerspectiveCamera* camera = _scene.GetMainCamera();
	builder.SetUniformBuffer(0, &camera->Matrices);
	builder.SetTextureBuffer(1, sdfTexture);
	builder.SetUniformBuffer(2, &_sdfParams);
	builder.SetTextureBuffer(3, depthForSampling);
	builder.SetTextureBuffer(4, _sdfShadowTexture, 0, VK_IMAGE_LAYOUT_GENERAL);
	auto& resources = builder.Build();

	commandBuffer.BindPipeline(_sdfShadowPipeline.get());
	commandBuffer.BindDescriptorSet(renderFrame,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		*_sdfShadowShader, 0, resources);

	uint32_t dispatchX = (_screenExtent.width / 2 + 7) / 8;
	uint32_t dispatchY = (_screenExtent.height / 2 + 7) / 8;
	commandBuffer.Dispatch(dispatchX, dispatchY, 1);

	commandBuffer.TransitionImageLayout(*_sdfShadowTexture->GetImage().lock(),
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// Transition depth back for subsequent passes
	commandBuffer.TransitionImageLayout(*depthTexture->GetImage().lock(),
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

	commandBuffer.EndDebugMarker();

	UpdateGUI();
}