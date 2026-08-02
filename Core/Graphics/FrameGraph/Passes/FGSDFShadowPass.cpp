#include "stdafx.h"
#include "FGSDFShadowPass.h"
#include "FGShadowPass.h"
#include "FGDepthPrePass.h"
#include "FGResolvePass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/RenderExecutor.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/SDFGenerator.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/Vulkans/Device.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Foundation/Scene.h"
#include "Foundation/Entity.h"
#include "Components/Light.h"
#include "Components/PerspectiveCamera.h"

using namespace Core;

static constexpr uint32_t DEBUG_SLICE_HEIGHT = 256;

FGSDFShadowPass::FGSDFShadowPass(Device& device, Scene& scene, VkExtent2D screenExtent,
	VkSampleCountFlagBits msaaSamples, FGShadowPass& shadowPass)
	: _device(device)
	, _scene(scene)
	, _screenExtent(screenExtent)
	, _msaaSamples(msaaSamples)
	, _shadowPass(shadowPass)
{
	_sdfGenerator = make_unique<SDFGenerator>(device);

	_sdfShadowShader = _device.GetResourceManager().LoadShader("Shaders/sdfShadow.comp.spv");
	_sdfShadowPipeline = make_unique<Pipeline>(_device, _sdfShadowShader.Get());

	_volumeSliceShader = _device.GetResourceManager().LoadShader("Shaders/sdfVolumeSlice.comp.spv");
	_volumeSlicePipeline = make_unique<Pipeline>(_device, _volumeSliceShader.Get());
}

FGSDFShadowPass::~FGSDFShadowPass()
{
	if (_sdfShadowImGuiDS != VK_NULL_HANDLE)
		ImGui_ImplVulkan_RemoveTexture(_sdfShadowImGuiDS);
	if (_volumeSliceImGuiDS != VK_NULL_HANDLE)
		ImGui_ImplVulkan_RemoveTexture(_volumeSliceImGuiDS);
}

void FGSDFShadowPass::UpdateSDFParams()
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
	_sdfParams.MaxSteps       = _maxSteps;
	_sdfParams.MinDistance    = _minDistance;
	_sdfParams.MaxDistance    = _maxDistance;
	_sdfParams.ShadowSoftness = _shadowSoftness;
	_sdfParams.PaddingFactor  = 0.1f;

	// Copy transition parameters from the shadow pass
	auto* shadowUniform = _shadowPass.GetShadowBuffer();
	_sdfParams.SDFTransitionDistance = shadowUniform->SDFTransitionDistance;
	_sdfParams.SDFTransitionRange    = shadowUniform->SDFTransitionRange;
}

void FGSDFShadowPass::Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
	RenderExecutor& renderExecutor)
{
	_sliceReady = false;

	// Deferred save: the SDF generated on a previous frame is now complete, so
	// it's safe to read back the image/bounds and write to disk.
	if (_savePending)
	{
		_savePending = false;
		_sdfGenerator->SaveToFile(SDF_VOLUME_DIM);
	}

	if (_regenerateRequested || !_sdfGenerator->IsGenerated())
	{
		bool tryLoad = !_regenerateRequested;
		_regenerateRequested = false;

		if (!tryLoad || !_sdfGenerator->TryLoadFromFile(SDF_VOLUME_DIM))
		{
			// Rare event: generation creates/resizes pool-owned resources, so it
			// runs here on the main thread with its own synchronous submit
			// instead of inside the graph's recording window.
			auto* batch = renderExecutor.GetRendererBatch();
			if (batch && batch->GetDrawCommandCount() > 0)
			{
				auto& commandBuffer = _device.BeginSingleTimeCommands();
				commandBuffer.BeginDebugMarker("SDF Volume Generation (GPU)");
				_sdfGenerator->Generate(frameResources, renderExecutor,
					commandBuffer, SDF_VOLUME_DIM);
				commandBuffer.EndDebugMarker();
				_device.EndSingleTimeCommands(commandBuffer);

				_savePending = true;
			}
			// else: the batch isn't built yet (startup); retried next frame
			// because IsGenerated() stays false.
		}
	}

	RenderTargetDesc sdfShadowDesc{};
	sdfShadowDesc.extent = {
		_screenExtent.width / 2,
		_screenExtent.height / 2
	};
	sdfShadowDesc.format  = VK_FORMAT_R8_UNORM;
	sdfShadowDesc.usage   = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	sdfShadowDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	sdfShadowDesc.aspect  = VK_IMAGE_ASPECT_COLOR_BIT;
	
	// This may be sampled before the first compute production,
	// so start it in the layout the consumer expects.
	sdfShadowDesc.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	auto sdfShadowTexture = frameResources.GetOrCreateRenderTarget(RT_SDF_SHADOW, sdfShadowDesc);

	// Volume raytrace debug texture - match screen aspect ratio
	float aspect = static_cast<float>(_screenExtent.width) / static_cast<float>(_screenExtent.height);
	uint32_t sliceWidth = static_cast<uint32_t>(DEBUG_SLICE_HEIGHT * aspect);

	RenderTargetDesc sliceDesc{};
	sliceDesc.extent  = { sliceWidth, DEBUG_SLICE_HEIGHT };
	sliceDesc.format  = VK_FORMAT_R8G8B8A8_UNORM;
	sliceDesc.usage   = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	sliceDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	sliceDesc.aspect  = VK_IMAGE_ASPECT_COLOR_BIT;
	sliceDesc.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	auto volumeSliceTexture = frameResources.GetOrCreateRenderTarget(RT_SDF_VOLUME_SLICE, sliceDesc);

	// No SDF volume yet (no cache file and the batch wasn't ready): declare
	// nothing so the pass culls itself this frame.
	if (!_sdfGenerator->GetSDFTexture().IsValid())
		return;

	// The depth this pass consumes: the resolved depth when MSAA is on, the main
	// depth otherwise. Both were declared by the earlier graph passes.
	const char* depthName = _msaaSamples != VK_SAMPLE_COUNT_1_BIT
		? FGResolvePass::RT_RESOLVED_DEPTH
		: FGDepthPrePass::RT_MAIN_DEPTH;
	_depth = builder.GetTexture(depthName);
	builder.Read(_depth, TextureAccess::SampledCompute);

	// Import: cross-queue (compute write, graphics read) so aliasing gives
	// nothing, and ImGui previews it by name.
	_sdfShadow = builder.ImportTexture(RT_SDF_SHADOW, sdfShadowTexture);
	builder.Write(_sdfShadow, TextureAccess::StorageComputeWrite);

	UpdateSDFParams();

	_camera = builder.ImportBuffer(UB_CAMERA,
		frameResources.GetOrCreateUniformBuffer<CameraBuffer>(UB_CAMERA));
	builder.Read(_camera, BufferAccess::UniformCompute);

	auto sdfParamsHandle =
		frameResources.GetOrCreateUniformBuffer<SDFShadowUniform>("SDFShadowPass.Params");
	sdfParamsHandle.Get().Update(_sdfParams);
	_sdfParamsBuffer = builder.ImportBuffer("SDFShadowPass.Params", sdfParamsHandle);
	builder.Read(_sdfParamsBuffer, BufferAccess::UniformCompute);

	// Volume raytrace debug view (ImGui samples it in the GUI pass).
	if (PerspectiveCamera* camera = _scene.GetMainCamera())
	{
		// Import: debug-only, sampled solely by ImGui — no in-graph reader, so a
		// transient would be culled.
		_volumeSlice = builder.ImportTexture(RT_SDF_VOLUME_SLICE, volumeSliceTexture);
		builder.Write(_volumeSlice, TextureAccess::StorageComputeWrite);

		glm::mat4 viewMatrix = camera->Matrices.View;
		glm::vec3 forward = -glm::vec3(viewMatrix[0][2], viewMatrix[1][2], viewMatrix[2][2]);
		glm::vec3 right   = glm::vec3(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]);
		glm::vec3 up      = glm::vec3(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);

		_slicePushConstants = {
			glm::vec4(camera->Matrices.Position, 0.0f),
			glm::vec4(forward, 0.0f),
			glm::vec4(right, 0.0f),
			glm::vec4(up, 0.0f),
			camera->GetFieldOfView(),
			200.0f,
			_debugMaxSteps,
			_debugHitThreshold,
			0.1f
		};
		_sliceReady = true;
	}
}

void FGSDFShadowPass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
	auto sdfTexture = _sdfGenerator->GetSDFTexture();

	// SDF shadow mask dispatch. The graph already transitioned the mask to
	// GENERAL and made the depth visible to compute.
	auto& sdfShadowShader = _sdfShadowShader.Get();
	auto builder = context.CreateDescriptorSetBuilder(sdfShadowShader, 0);
	builder.SetUniformBuffer(0, context.GetBuffer(_camera));
	builder.SetTextureBuffer(1, sdfTexture);
	builder.SetUniformBuffer(2, context.GetBuffer(_sdfParamsBuffer));
	builder.SetTextureBuffer(3, context.GetTexture(_depth));
	builder.SetTextureBuffer(4, context.GetTexture(_sdfShadow), 0, VK_IMAGE_LAYOUT_GENERAL);
	builder.SetStorageBuffer(5, *_sdfGenerator->GetBoundsBuffer());
	auto& resources = builder.Build();

	commandBuffer.BindPipeline(_sdfShadowPipeline.get());
	commandBuffer.BindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE,
		sdfShadowShader,
		resources);

	uint32_t dispatchX = (_screenExtent.width / 2 + 7) / 8;
	uint32_t dispatchY = (_screenExtent.height / 2 + 7) / 8;
	commandBuffer.Dispatch(dispatchX, dispatchY, 1);

	if (_sliceReady)
	{
		commandBuffer.BeginDebugMarker("SDF Volume Raytrace Debug");

		auto& volumeSliceShader = _volumeSliceShader.Get();
		auto sliceBuilder = context.CreateDescriptorSetBuilder(volumeSliceShader, 0);
		sliceBuilder.SetTextureBuffer(0, sdfTexture);
		sliceBuilder.SetTextureBuffer(1, context.GetTexture(_volumeSlice), 0, VK_IMAGE_LAYOUT_GENERAL);
		sliceBuilder.SetStorageBuffer(2, *_sdfGenerator->GetBoundsBuffer());
		auto& sliceResources = sliceBuilder.Build();

		commandBuffer.BindPipeline(_volumeSlicePipeline.get());
		commandBuffer.BindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, volumeSliceShader,
			sliceResources);
		commandBuffer.PushConstants(volumeSliceShader, 0, _slicePushConstants);

		float    aspect     = static_cast<float>(_screenExtent.width) / static_cast<float>(_screenExtent.height);
		uint32_t sliceWidth = static_cast<uint32_t>(DEBUG_SLICE_HEIGHT * aspect);
		commandBuffer.Dispatch((sliceWidth + 7) / 8, (DEBUG_SLICE_HEIGHT + 7) / 8, 1);

		commandBuffer.EndDebugMarker();
	}
}

void FGSDFShadowPass::OnGUI(RenderFrame& renderFrame)
{
	if (!ImGui::CollapsingHeader("SDF Shadow"))
		return;

	if (ImGui::Button("Generate SDF Texture"))
		_regenerateRequested = true;

	if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::SliderFloat("Shadow Softness", &_shadowSoftness, 1.0f, 32.0f);
		ImGui::SliderFloat("Min Distance",    &_minDistance,    0.0001f, 0.1f, "%.4f");
		ImGui::SliderFloat("Max Distance",    &_maxDistance,    10.0f, 500.0f);
		ImGui::SliderInt  ("Max Steps",       &_maxSteps,       8, 128);

		ImGui::TextDisabled("Cache: %s", _sdfGenerator->GetCachePath().c_str());
	}

	float aspect     = static_cast<float>(_screenExtent.width) / static_cast<float>(_screenExtent.height);
	float previewWidth = DEBUG_SLICE_HEIGHT * aspect;

	// Preview targets are looked up by name: the pass keeps no pool handles.
	auto sdfShadowTexture = renderFrame.GetResources().GetRenderTarget(RT_SDF_SHADOW);
	auto volumeSliceTexture = renderFrame.GetResources().GetRenderTarget(RT_SDF_VOLUME_SLICE);

	if (sdfShadowTexture.IsValid() && ImGui::CollapsingHeader("Shadow Map", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (_sdfShadowImGuiDS == VK_NULL_HANDLE)
		{
			_sdfShadowImGuiDS = ImGui_ImplVulkan_AddTexture(
				sdfShadowTexture.Get().GetVkSampler(),
				sdfShadowTexture.Get().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}
		ImGui::Image(static_cast<ImTextureID>(_sdfShadowImGuiDS),
			ImVec2(previewWidth, DEBUG_SLICE_HEIGHT));
	}

	if (volumeSliceTexture.IsValid() && ImGui::CollapsingHeader("Volume Raytrace", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (_volumeSliceImGuiDS == VK_NULL_HANDLE)
		{
			_volumeSliceImGuiDS = ImGui_ImplVulkan_AddTexture(
				volumeSliceTexture.Get().GetVkSampler(),
				volumeSliceTexture.Get().GetImageView(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}
		ImGui::SliderFloat("Hit Threshold", &_debugHitThreshold, 0.001f, 0.1f, "%.4f");
		ImGui::SliderInt  ("Ray Max Steps", &_debugMaxSteps,      32, 256);
		ImGui::Image(static_cast<ImTextureID>(_volumeSliceImGuiDS),
			ImVec2(previewWidth, DEBUG_SLICE_HEIGHT));
	}

	ImGui::Separator();
}
