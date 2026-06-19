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

static constexpr uint32_t DEBUG_SLICE_HEIGHT = 256;

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

	_depthResolveShader = _device.GetResourceCache().RequestShader("Shaders/depthResolve.comp.spv");
	_depthResolvePipeline = make_unique<Pipeline>(_device, *_depthResolveShader);

	_volumeSliceShader = _device.GetResourceCache().RequestShader("Shaders/sdfVolumeSlice.comp.spv");
	_volumeSlicePipeline = make_unique<Pipeline>(_device, *_volumeSliceShader);
}

SDFShadowPass::~SDFShadowPass()
{
	if (_sdfShadowImGuiDS != VK_NULL_HANDLE)
		ImGui_ImplVulkan_RemoveTexture(_sdfShadowImGuiDS);
	if (_volumeSliceImGuiDS != VK_NULL_HANDLE)
		ImGui_ImplVulkan_RemoveTexture(_volumeSliceImGuiDS);
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

	// Volume raytrace debug texture ? match screen aspect ratio
	float aspect = static_cast<float>(_screenExtent.width) / static_cast<float>(_screenExtent.height);
	uint32_t sliceWidth = static_cast<uint32_t>(DEBUG_SLICE_HEIGHT * aspect);

	RenderTargetDesc sliceDesc{};
	sliceDesc.extent = { sliceWidth, DEBUG_SLICE_HEIGHT };
	sliceDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
	sliceDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	sliceDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	sliceDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;

	_volumeSliceTexture = renderFrame.GetOrCreateRenderTarget(RT_SDF_VOLUME_SLICE, sliceDesc);
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

void SDFShadowPass::RenderVolumeSlice(RenderFrame& renderFrame, CommandBuffer& commandBuffer)
{
	auto sdfTexture = _sdfGenerator->GetSDFTexture();
	auto* boundsBuffer = _sdfGenerator->GetBoundsBuffer();
	if (!sdfTexture || !_volumeSliceTexture || !boundsBuffer)
		return;

	auto& sliceImage = *_volumeSliceTexture->GetImage().lock();
	commandBuffer.TransitionImageLayout(sliceImage,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	PerspectiveCamera* camera = _scene.GetMainCamera();
	glm::mat4 viewMatrix = camera->Matrices.View;
	glm::vec3 camPos = camera->Matrices.Position;

	glm::vec3 forward = -glm::vec3(viewMatrix[0][2], viewMatrix[1][2], viewMatrix[2][2]);
	glm::vec3 right = glm::vec3(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]);
	glm::vec3 up = glm::vec3(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);

	struct VolumeRaytracePushConstants {
		glm::vec4 cameraPos;
		glm::vec4 cameraForward;
		glm::vec4 cameraRight;
		glm::vec4 cameraUp;
		float fov;
		float maxDistance;
		int32_t maxSteps;
		float hitThreshold;
		float paddingFactor;
	} pc = {
		glm::vec4(camPos, 0.0f),
		glm::vec4(forward, 0.0f),
		glm::vec4(right, 0.0f),
		glm::vec4(up, 0.0f),
		camera->GetFieldOfView(),
		200.0f,
		_debugMaxSteps,
		_debugHitThreshold,
		0.1f
	};

	auto sliceBuilder = renderFrame.CreateDescriptorSetBuilder(*_volumeSliceShader, 0);
	sliceBuilder.SetTextureBuffer(0, sdfTexture);
	sliceBuilder.SetTextureBuffer(1, _volumeSliceTexture, 0, VK_IMAGE_LAYOUT_GENERAL);
	sliceBuilder.SetStorageBuffer(2, boundsBuffer);
	auto& sliceResources = sliceBuilder.Build();

	commandBuffer.BindPipeline(_volumeSlicePipeline.get());
	commandBuffer.BindDescriptorSet(renderFrame, VK_PIPELINE_BIND_POINT_COMPUTE,
		*_volumeSliceShader, 0, sliceResources);
	commandBuffer.PushConstants(*_volumeSliceShader, 0, &pc);

	float aspect = static_cast<float>(_screenExtent.width) / static_cast<float>(_screenExtent.height);
	uint32_t sliceWidth = static_cast<uint32_t>(DEBUG_SLICE_HEIGHT * aspect);

	commandBuffer.Dispatch((sliceWidth + 7) / 8, (DEBUG_SLICE_HEIGHT + 7) / 8, 1);

	commandBuffer.TransitionImageLayout(sliceImage,
		VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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

	_sdfParams.VolumeResolution = vec4(
		static_cast<float>(SDF_VOLUME_DIM),
		static_cast<float>(SDF_VOLUME_DIM),
		static_cast<float>(SDF_VOLUME_DIM),
		_maxDistance);
	_sdfParams.MaxSteps = _maxSteps;
	_sdfParams.MinDistance = _minDistance;
	_sdfParams.MaxDistance = _maxDistance;
	_sdfParams.ShadowSoftness = _shadowSoftness;
	_sdfParams.PaddingFactor = 0.1f;
}

void SDFShadowPass::UpdateGUI()
{
	// Main menu bar with Debug menu
	if (ImGui::BeginMainMenuBar())
	{
		if (ImGui::BeginMenu("Debug"))
		{
			ImGui::MenuItem("SDF Shadow", nullptr, &_showSDFShadowWindow);
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
	}

	if (!_showSDFShadowWindow)
		return;

	if (!ImGui::Begin("SDF Shadow Debug", &_showSDFShadowWindow))
	{
		ImGui::End();
		return;
	}

	// Shadow parameters
	if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::SliderFloat("Shadow Softness", &_shadowSoftness, 1.0f, 32.0f);
		ImGui::SliderFloat("Min Distance", &_minDistance, 0.0001f, 0.1f, "%.4f");
		ImGui::SliderFloat("Max Distance", &_maxDistance, 10.0f, 500.0f);
		ImGui::SliderInt("Max Steps", &_maxSteps, 8, 128);

		if (ImGui::Button("Regenerate SDF"))
		{
			_sdfGenerated = false;
		}
	}

	float aspect = static_cast<float>(_screenExtent.width) / static_cast<float>(_screenExtent.height);
	float previewWidth = DEBUG_SLICE_HEIGHT * aspect;

	// Shadow map preview
	if (_sdfShadowTexture && ImGui::CollapsingHeader("Shadow Map", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (_sdfShadowImGuiDS == VK_NULL_HANDLE)
		{
			_sdfShadowImGuiDS = ImGui_ImplVulkan_AddTexture(
				_sdfShadowTexture->GetSampler()->GetSampler(),
				_sdfShadowTexture->GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}

		ImGui::Image(static_cast<ImTextureID>(_sdfShadowImGuiDS),
			ImVec2(previewWidth, DEBUG_SLICE_HEIGHT));
	}

	// Volume raytrace preview
	if (_volumeSliceTexture && ImGui::CollapsingHeader("Volume Raytrace", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (_volumeSliceImGuiDS == VK_NULL_HANDLE)
		{
			_volumeSliceImGuiDS = ImGui_ImplVulkan_AddTexture(
				_volumeSliceTexture->GetSampler()->GetSampler(),
				_volumeSliceTexture->GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}

		ImGui::SliderFloat("Hit Threshold", &_debugHitThreshold, 0.001f, 0.1f, "%.4f");
		ImGui::SliderInt("Ray Max Steps", &_debugMaxSteps, 32, 256);
		ImGui::Image(static_cast<ImTextureID>(_volumeSliceImGuiDS),
			ImVec2(previewWidth, DEBUG_SLICE_HEIGHT));
	}

	ImGui::End();
}

void SDFShadowPass::Prepare()
{
}

void SDFShadowPass::Draw(RenderFrame& renderFrame, uint32_t imageIndex)
{
	auto* meshBufferManager = renderFrame.GetMeshBufferManager();
	if (!meshBufferManager || !_objectDataBuffer || !_transformBuffer)
		return;

	auto& commandBuffer = renderFrame.GetCommandBuffer();

	if (!_sdfGenerated)
	{
		commandBuffer.BeginDebugMarker("SDF Volume Generation (GPU)");
		_sdfGenerator->Generate(renderFrame, commandBuffer,
			*meshBufferManager,
			_objectDataBuffer, _transformBuffer,
			_drawCommandBuffer, _drawCommandCount,
			_instanceCount,
			SDF_VOLUME_DIM);
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

	commandBuffer.TransitionImageLayout(*depthTexture->GetImage().lock(),
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

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
	builder.SetStorageBuffer(5, _sdfGenerator->GetBoundsBuffer());
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

	if (_showSDFShadowWindow)
	{
		commandBuffer.BeginDebugMarker("SDF Volume Raytrace Debug");
		RenderVolumeSlice(renderFrame, commandBuffer);
		commandBuffer.EndDebugMarker();
	}

	commandBuffer.TransitionImageLayout(*depthTexture->GetImage().lock(),
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

	UpdateGUI();
}