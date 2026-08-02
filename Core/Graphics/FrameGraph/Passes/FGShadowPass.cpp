#include "stdafx.h"
#include "FGShadowPass.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/RenderExecutor.h"
#include "Graphics/FrustumCuller.h"
#include "Graphics/ResourceManager.h"
#include "Graphics/Material.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/PipelineState.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Foundation/Scene.h"
#include "Foundation/Entity.h"
#include "Components/PerspectiveCamera.h"
#include "Components/Light.h"

using namespace Core;

FGShadowPass::FGShadowPass(Device& device, Scene& scene, VkFormat depthFormat)
	: _device(device)
	, _scene(scene)
{
	_shadowExtent = { SHADOW_MAP_DIM, SHADOW_MAP_DIM };

	_pipelineState = make_unique<PipelineState>();

	// Enable depth bias (actual values set dynamically via vkCmdSetDepthBias)
	_pipelineState->GetRasterizationStateCreateInfo().depthBiasEnable = VK_TRUE;

	// The material is registered in the cache/material table; we only need its
	// shader handle here, so it isn't kept as a member.
	auto shadowMaterial = _device.GetResourceManager().LoadMaterial("shadow", "Shadow");
	_shadowShader = shadowMaterial.Get().GetShaderHandle();

	// Depth-only dynamic-rendering pipeline.
	PipelineRenderingDesc renderingDesc;
	renderingDesc.depthFormat = depthFormat;
	_pipeline = make_unique<Pipeline>(device, renderingDesc, _shadowShader.Get(), *_pipelineState);
}

FGShadowPass::~FGShadowPass() = default;

std::array<glm::vec3, 8> FGShadowPass::GetFrustumCornersWorldSpace(const glm::mat4& viewProj)
{
	const glm::mat4 inv = glm::inverse(viewProj);

	std::array<glm::vec3, 8> corners;
	uint32_t idx = 0;
	for (uint32_t x = 0; x < 2; ++x)
	{
		for (uint32_t y = 0; y < 2; ++y)
		{
			for (uint32_t z = 0; z < 2; ++z)
			{
				const glm::vec4 pt = inv * glm::vec4(
					2.0f * x - 1.0f,
					2.0f * y - 1.0f,
					static_cast<float>(z), // Vulkan depth [0,1]
					1.0f);
				corners[idx++] = glm::vec3(pt) / pt.w;
			}
		}
	}
	return corners;
}

void FGShadowPass::UpdateCascades(PerspectiveCamera* camera)
{
	float nearClip = camera->GetNearPlane();
	float farClip = camera->GetFarPlane();
	float clipRange = farClip - nearClip;

	float minZ = nearClip;
	float maxZ = nearClip + clipRange;
	float range = maxZ - minZ;
	float ratio = maxZ / minZ;

	// Calculate cascade split depths using practical split scheme
	std::array<float, SHADOW_MAP_CASCADE_COUNT> cascadeSplits;
	for (uint32_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; ++i)
	{
		float p = static_cast<float>(i + 1) / static_cast<float>(SHADOW_MAP_CASCADE_COUNT);
		float log = minZ * std::pow(ratio, p);
		float uniform = minZ + range * p;
		float d = _cascadeSplitLambda * (log - uniform) + uniform;
		cascadeSplits[i] = (d - nearClip) / clipRange;
	}

	// Determine effective cascade count: cascades whose near plane is beyond
	// the SDF transition distance are skipped (SDF takes over there).
	// Include the cascade that contains the transition zone so the blend works.
	float transitionFar = _shadowBuffer.SDFTransitionDistance
		+ _shadowBuffer.SDFTransitionRange * 0.5f;

	uint32_t effectiveCascadeCount = SHADOW_MAP_CASCADE_COUNT;
	float lastSplitDistForCount = 0.0f;
	for (uint32_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; ++i)
	{
		float cascadeNearDist = nearClip + lastSplitDistForCount * clipRange;
		if (cascadeNearDist >= transitionFar)
		{
			effectiveCascadeCount = i;
			break;
		}
		lastSplitDistForCount = cascadeSplits[i];
	}
	effectiveCascadeCount = std::max<uint32_t>(effectiveCascadeCount, 1u);

	_shadowBuffer.CascadeCount = effectiveCascadeCount;

	// Get light direction
	auto lights = _scene.GetComponents<Light>();
	if (lights.empty())
		return;

	auto light = lights[0];
	auto& lightTransform = light->GetEntity().GetTransform();
	glm::mat4 lightMatrix = lightTransform.GetMatrix();
	glm::vec3 lightDir = glm::normalize(glm::vec3(lightMatrix[2]));

	float shadowMapSize = static_cast<float>(SHADOW_MAP_DIM);

	// Build cascade matrices for active cascades only.
	// The last active cascade's far plane is clamped to transitionFar so
	// the cascade tightly fits the CSM-only region.
	float lastSplitDist = 0.0f;
	for (uint32_t i = 0; i < effectiveCascadeCount; ++i)
	{
		float splitDist = cascadeSplits[i];

		float cascadeNear = nearClip + lastSplitDist * clipRange;
		float cascadeFar = nearClip + splitDist * clipRange;

		// Tighten the last active cascade to the transition boundary.
		// This dramatically improves shadow resolution near the transition zone.
		if (i == effectiveCascadeCount - 1)
		{
			cascadeFar = std::min(cascadeFar, transitionFar);
			splitDist = (cascadeFar - nearClip) / clipRange;
		}

		glm::mat4 cascadeProj = glm::perspective(
			camera->GetFieldOfView(),
			camera->GetAspectRatio(),
			cascadeNear, cascadeFar);
		// Flip Y to match Vulkan convention (same as camera)
		cascadeProj[1][1] *= -1;

		glm::mat4 viewProj = cascadeProj * camera->GetView();
		auto corners = GetFrustumCornersWorldSpace(viewProj);

		// Calculate frustum center
		glm::vec3 frustumCenter(0.0f);
		for (auto& corner : corners)
		{
			frustumCenter += corner;
		}
		frustumCenter /= 8.0f;

		// Calculate bounding sphere radius for stable shadow maps
		float radius = 0.0f;
		for (auto& corner : corners)
		{
			float distance = glm::length(corner - frustumCenter);
			radius = glm::max(radius, distance);
		}
		radius = std::ceil(radius * 16.0f) / 16.0f;

		glm::vec3 maxExtents = glm::vec3(radius);
		glm::vec3 minExtents = -maxExtents;

		// Light view matrix looking at frustum center from light direction
		glm::mat4 lightViewMatrix = glm::lookAt(
			frustumCenter - lightDir * (-minExtents.z),
			frustumCenter,
			glm::vec3(0.0f, 1.0f, 0.0f));

		glm::mat4 lightOrthoMatrix = glm::ortho(
			minExtents.x, maxExtents.x,
			minExtents.y, maxExtents.y,
			0.0f, maxExtents.z - minExtents.z);

		// Texel snapping: round the light-space origin to shadow map texel boundaries
		// This prevents shadow shaking/swimming when the camera moves
		glm::mat4 shadowMatrix = lightOrthoMatrix * lightViewMatrix;
		glm::vec4 shadowOrigin = shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
		shadowOrigin *= shadowMapSize / 2.0f;

		glm::vec4 roundedOrigin = glm::round(shadowOrigin);
		glm::vec4 roundOffset = roundedOrigin - shadowOrigin;
		roundOffset *= 2.0f / shadowMapSize;
		roundOffset.z = 0.0f;
		roundOffset.w = 0.0f;

		lightOrthoMatrix[3] += roundOffset;

		// Store cascade data
		_shadowBuffer.SplitDepth[i].value = (nearClip + splitDist * clipRange) * -1.0f;
		_shadowBuffer.ViewProjection[i] = lightOrthoMatrix * lightViewMatrix;

		_cascadeViews[i].View = lightViewMatrix;
		_cascadeViews[i].Projection = lightOrthoMatrix;

		lastSplitDist = splitDist;
	}

	// Inactive cascades: mark with a split depth that no fragment will reach,
	// so the cascade selection loop never picks them.
	for (uint32_t i = effectiveCascadeCount; i < SHADOW_MAP_CASCADE_COUNT; ++i)
	{
		_shadowBuffer.SplitDepth[i].value = -1e9f;
	}
}

void FGShadowPass::Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
	RenderExecutor& renderExecutor)
{
	_cullers.fill(nullptr);

	PerspectiveCamera* camera = _scene.GetMainCamera();
	if (!camera)
		return; // nothing declared: the pass culls itself this frame

	UpdateCascades(camera);

	auto& shadowUniform = frameResources.GetOrCreateUniformBuffer<ShadowUniform>(UB_SHADOW).Get();
	shadowUniform.Update(_shadowBuffer);

	RenderTargetDesc depthDesc{};
	depthDesc.extent = _shadowExtent;
	depthDesc.format = VK_FORMAT_UNDEFINED; // auto-selected depth format
	depthDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	depthDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	depthDesc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
	depthDesc.arrayLayers = SHADOW_MAP_CASCADE_COUNT;
	depthDesc.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; // Always 2D_ARRAY for sampler2DArray
	depthDesc.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	auto shadowTexture = frameResources.GetOrCreateRenderTarget(RT_SHADOW_DEPTH, depthDesc);

	_shadowDepth = builder.ImportTexture(RT_SHADOW_DEPTH, shadowTexture);
	builder.Write(_shadowDepth, TextureAccess::DepthWrite);

	// Renders into one array layer at a time via per-cascade layer views, which
	// the declared-attachment path (whole-texture view) cannot express.
	builder.SetManualRendering();

	// The culling dispatches write indirect draw buffers the graph cannot see.
	builder.SetSideEffect();

	for (uint32_t i = 0; i < _shadowBuffer.CascadeCount; ++i)
	{
		// Each cascade binds binding 0 with a different view, and all four
		// descriptor sets are consumed after submit, so they need separate buffers.
		string cascadeName = "ShadowPass.Cascade" + std::to_string(i);
		auto cascadeHandle = frameResources.GetOrCreateUniformBuffer<CameraBuffer>(cascadeName);
		cascadeHandle.Get().Update(_cascadeViews[i]);

		_cascadeBuffers[i] = builder.ImportBuffer(cascadeName, cascadeHandle);
		builder.Read(_cascadeBuffers[i], BufferAccess::UniformVertex);

		_cullers[i] = renderExecutor.PrepareFrustumCuller(_cascadeViews[i]);
	}
}

void FGShadowPass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
	auto& shadowTexture = context.GetTexture(_shadowDepth);
	auto& shader = _shadowShader.Get();
	auto& executor = context.GetRenderExecutor();

	commandBuffer.SetViewportAndScissor(_shadowExtent);

	for (uint32_t cascadeIndex = 0; cascadeIndex < SHADOW_MAP_CASCADE_COUNT; ++cascadeIndex)
	{
		VkRenderingAttachmentInfo depthAttachment{};
		depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depthAttachment.imageView = shadowTexture.GetLayerImageView(cascadeIndex);
		depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

		VkRenderingInfo renderingInfo{};
		renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		renderingInfo.renderArea = { { 0, 0 }, _shadowExtent };
		renderingInfo.layerCount = 1;
		renderingInfo.pDepthAttachment = &depthAttachment;

		// Skip cascades beyond the SDF transition zone but still clear them
		// so stale depth doesn't show up in the debug viewer.
		if (cascadeIndex >= _shadowBuffer.CascadeCount || _cullers[cascadeIndex] == nullptr)
		{
			string clearName = "Shadow Cascade " + std::to_string(cascadeIndex) + " Clear (skipped)";
			commandBuffer.BeginDebugMarker(clearName.c_str());
			commandBuffer.BeginRendering(renderingInfo);
			commandBuffer.EndRendering();
			commandBuffer.EndDebugMarker();
			continue;
		}

		auto builder = context.CreateDescriptorSetBuilder(shader, 0);
		builder.SetUniformBuffer(0, context.GetBuffer(_cascadeBuffers[cascadeIndex]));

		string passName = "Shadow Cascade " + std::to_string(cascadeIndex);
		commandBuffer.BeginDebugMarker(passName.c_str());

		commandBuffer.SetDepthBias(_depthBiasConstant, _depthBiasClamp, _depthBiasSlope);

		executor.FrustumCullAndDraw(commandBuffer, *_cullers[cascadeIndex],
			shader, *_pipeline, renderingInfo, builder, nullptr);

		commandBuffer.EndDebugMarker();
	}
}

void FGShadowPass::OnGUI(RenderFrame& renderFrame)
{
	if (!ImGui::CollapsingHeader("Shadow Pass"))
		return;

	ImGui::SeparatorText("PCSS Settings");
	{
		ImGui::SliderFloat("Light Size", &_shadowBuffer.LightSize, 0.001f, 0.2f, "%.3f");
		ImGui::SliderFloat("Min Filter Radius", &_shadowBuffer.MinFilterRadius, 0.1f, 5.0f, "%.1f");
		ImGui::SliderFloat("Max Filter Radius", &_shadowBuffer.MaxFilterRadius, 1.0f, 30.0f, "%.1f");
	}

	ImGui::SeparatorText("Depth Bias");
	{
		ImGui::SliderFloat("Constant Factor", &_depthBiasConstant, 0.0f, 4.0f, "%.2f");
		ImGui::SliderFloat("Slope Factor", &_depthBiasSlope, 0.0f, 4.0f, "%.2f");
		ImGui::SliderFloat("Clamp", &_depthBiasClamp, 0.0f, 0.1f, "%.4f");
	}

	ImGui::SeparatorText("Cascade Settings");
	{
		ImGui::SliderFloat("Split Lambda", &_cascadeSplitLambda, 0.0f, 1.0f, "%.2f");
		ImGui::SliderFloat("Blend Factor", &_shadowBuffer.CascadeBlendFactor, 0.0f, 1.0f, "%.2f");
		ImGui::SetItemTooltip("Fraction of cascade range used for blending (0 = off, 0.3 = 30%%)");
	}

	ImGui::SeparatorText("SDF Shadow");
	{
		ImGui::SliderFloat("Transition Distance", &_shadowBuffer.SDFTransitionDistance,
			1.0f, 200.0f, "%.1f m");
		ImGui::SetItemTooltip("View-space distance where shadow switches from CSM to SDF");
		ImGui::SliderFloat("Transition Range", &_shadowBuffer.SDFTransitionRange,
			0.0f, 50.0f, "%.1f m");
		ImGui::SetItemTooltip("Width of the smooth blend zone (0 = hard switch)");
	}

	if (ImGui::Button("Reset Defaults"))
	{
		_shadowBuffer.LightSize = 0.04f;
		_shadowBuffer.MinFilterRadius = 0.5f;
		_shadowBuffer.MaxFilterRadius = 10.0f;
		_shadowBuffer.CascadeBlendFactor = 0.3f;
		_shadowBuffer.SDFTransitionDistance = 30.0f;
		_shadowBuffer.SDFTransitionRange = 5.0f;
		_depthBiasConstant = 1.25f;
		_depthBiasSlope = 1.75f;
		_depthBiasClamp = 0.0f;
		_cascadeSplitLambda = 0.95f;
	}

	ImGui::SeparatorText("Cascaded Shadow Maps");
	{
		auto shadowTexture = renderFrame.GetResources().GetRenderTarget(RT_SHADOW_DEPTH);

		if (shadowTexture.IsValid())
		{
			if (!_csmDescriptorsCreated)
			{
				auto& shadowTex = shadowTexture.Get();
				auto vkSampler = shadowTex.GetVkSampler();
				for (uint32_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; ++i)
				{
					VkImageView layerView = shadowTex.GetLayerImageView(i);
					_csmDescriptorSets[i] = ImGui_ImplVulkan_AddTexture(
						vkSampler,
						layerView,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				}
				_csmDescriptorsCreated = true;
			}

			float previewSize = 200.0f;
			for (uint32_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; ++i)
			{
				ImGui::Text("Cascade %u (split: %.2f)",
					i, _shadowBuffer.SplitDepth[i].value * -1.0f);
				ImGui::Image((ImTextureID)_csmDescriptorSets[i],
					ImVec2(previewSize, previewSize));

				if (i < SHADOW_MAP_CASCADE_COUNT - 1)
					ImGui::SameLine();
			}
		}
		else
		{
			ImGui::Text("Shadow map not available");
		}
	}

	ImGui::Separator();
}
