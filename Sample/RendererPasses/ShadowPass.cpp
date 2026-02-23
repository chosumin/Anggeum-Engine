#include "stdafx.h"
#include "ShadowPass.h"
#include "Foundation/Scene.h"
#include "Components/PerspectiveCamera.h"
#include "Components/Light.h"
#include "Components/Mesh.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Material.h"
#include "Graphics/ResourceCache.h"

using namespace Core;

Core::ShadowPass::ShadowPass(Device& device, WorkerThreadManager& workerThreadManager,
	Scene& scene, VkFormat depthFormat, 
	ShadowUniform& shadowBuffer, TransformBatch& transformBatch)
	: RendererPass(device, workerThreadManager)
	, _scene(scene), _msaaSamples(VK_SAMPLE_COUNT_1_BIT), _shadowBuffer(shadowBuffer)
{
	_shadowExtent = { SHADOW_MAP_DIM, SHADOW_MAP_DIM };

	_renderPass->CreateDepthAttachment(depthFormat, _msaaSamples,
		VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
	_renderPass->CreateRenderPass();

	_shadowMaterial = _device.GetResourceCache().RequestMaterial("shadow", "Shadow");

	_rendererBatches = make_unique<RendererBatches>(device, transformBatch);

	auto meshes = _scene.GetComponents<Core::Mesh>();
	_rendererBatches->PrepareSingleBatch(_device,
		_shadowMaterial,
		*_renderPass, *_pipelineState, meshes);

	if (_device.IsGpuDrivenRenderingEnabled())
		_rendererBatches->PrepareGPUDrivenRendering(_device, false, _shadowExtent);
}

Core::ShadowPass::~ShadowPass()
{
}

std::array<glm::vec3, 8> Core::ShadowPass::GetFrustumCornersWorldSpace(const glm::mat4& viewProj)
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

void Core::ShadowPass::UpdateCascades(PerspectiveCamera* camera)
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

	// Get light direction
	auto lights = _scene.GetComponents<Light>();
	if (lights.empty())
		return;

	auto light = lights[0];
	auto& lightTransform = light->GetEntity().GetTransform();
	glm::mat4 lightMatrix = lightTransform.GetMatrix();
	glm::vec3 lightDir = glm::normalize(glm::vec3(lightMatrix[2]));

	// Build cascade matrices
	float lastSplitDist = 0.0f;
	for (uint32_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; ++i)
	{
		float splitDist = cascadeSplits[i];

		// Build a sub-frustum projection with this cascade's near/far planes
		float cascadeNear = nearClip + lastSplitDist * clipRange;
		float cascadeFar = nearClip + splitDist * clipRange;

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

		// Store cascade data
		_shadowBuffer.SplitDepth[i].value = (nearClip + splitDist * clipRange) * -1.0f;
		_shadowBuffer.ViewProjection[i] = lightOrthoMatrix * lightViewMatrix;

		_cascadeViews[i].View = lightViewMatrix;
		_cascadeViews[i].Projection = lightOrthoMatrix;

		lastSplitDist = splitDist;
	}
}

void Core::ShadowPass::EnsureRenderTargets(RenderFrame& renderFrame)
{
	RenderTargetDesc depthDesc{};
	depthDesc.extent = _shadowExtent;
	depthDesc.format = VK_FORMAT_UNDEFINED;
	depthDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	depthDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	depthDesc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
	depthDesc.arrayLayers = SHADOW_MAP_CASCADE_COUNT;
	depthDesc.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; // Always 2D_ARRAY for sampler2DArray

	renderFrame.GetOrCreateRenderTarget(RT_SHADOW_DEPTH, depthDesc);
}

void Core::ShadowPass::UpdateGUI(RenderFrame& renderFrame)
{
	ImGui::Begin("Shadow Maps");

	// CSM Shadow Map Debug View
	ImGui::SeparatorText("Cascaded Shadow Maps");
	{
		auto shadowTexture = renderFrame.GetRenderTarget(RT_SHADOW_DEPTH);

		if (shadowTexture)
		{
			// Create ImGui descriptor sets for each cascade layer (once)
			if (!_csmDescriptorsCreated)
			{
				auto sampler = shadowTexture->GetSampler();
				for (uint32_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; ++i)
				{
					VkImageView layerView = shadowTexture->GetLayerImageView(i);
					_csmDescriptorSets[i] = ImGui_ImplVulkan_AddTexture(
						sampler->GetSampler(),
						layerView,
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				}
				_csmDescriptorsCreated = true;
			}

			float previewSize = 200.0f;
			for (uint32_t i = 0; i < SHADOW_MAP_CASCADE_COUNT; ++i)
			{
				ImGui::Text("Cascade %u", i);
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

	ImGui::End();
}

void Core::ShadowPass::Prepare()
{
}

void Core::ShadowPass::Draw(RenderFrame& renderFrame, uint32_t imageIndex)
{
	EnsureRenderTargets(renderFrame);

	PerspectiveCamera* camera = _scene.GetMainCamera();
	if (!camera)
		return;

	UpdateCascades(camera);

	auto shader = _shadowMaterial->GetShaderPtr().lock();

	for (uint32_t cascadeIndex = 0; cascadeIndex < SHADOW_MAP_CASCADE_COUNT; ++cascadeIndex)
	{
		string fbName = "ShadowPass_Cascade" + std::to_string(cascadeIndex);

		auto* framebuffer = renderFrame.GetOrCreateFramebuffer(
			fbName, *_renderPass, { RT_SHADOW_DEPTH }, cascadeIndex);

		if (!framebuffer)
			continue;

		auto& commandBuffer = renderFrame.GetCommandBuffer();

		auto builder = renderFrame.CreateDescriptorSetBuilder(*shader, 0);
		builder.SetUniformBuffer(0, &_cascadeViews[cascadeIndex]);

		if (_device.IsGpuDrivenRenderingEnabled())
		{
			string cullingName = "Shadow Cascade " + std::to_string(cascadeIndex) + " Frustum Culling";
			commandBuffer.BeginDebugMarker(cullingName.c_str());
			_rendererBatches->DispatchFrustumOnlyCulling(
				renderFrame, commandBuffer, _cascadeViews[cascadeIndex]);
			commandBuffer.EndDebugMarker();

			string drawName = "Shadow Cascade " + std::to_string(cascadeIndex) + " Draw";
			commandBuffer.BeginDebugMarker(drawName.c_str());
			commandBuffer.SetViewportAndScissor(framebuffer->GetExtent());
			auto renderPassBeginInfo = _renderPass->CreateRenderPassBeginInfo(*framebuffer);
			commandBuffer.BeginRenderPass(renderPassBeginInfo);

			_rendererBatches->DrawIndirect(renderFrame, commandBuffer, builder,
			[&](shared_ptr<Material> sharedMaterial) {});

			commandBuffer.EndRenderPass();
			commandBuffer.EndDebugMarker();
		}
		else
		{
			string drawName = "Shadow Cascade " + std::to_string(cascadeIndex) + " Draw";
			commandBuffer.BeginDebugMarker(drawName.c_str());
			commandBuffer.SetViewportAndScissor(framebuffer->GetExtent());
			auto renderPassBeginInfo = _renderPass->CreateRenderPassBeginInfo(*framebuffer);
			commandBuffer.BeginRenderPass(renderPassBeginInfo);

			_rendererBatches->Draw(renderFrame, commandBuffer, builder,
			[&](shared_ptr<Material> sharedMaterial, shared_ptr<SubMesh> subMesh) {});

			commandBuffer.EndRenderPass();
			commandBuffer.EndDebugMarker();
		}
	}

	UpdateGUI(renderFrame);
}
