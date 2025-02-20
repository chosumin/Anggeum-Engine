#include "stdafx.h"
#include "ShadowRenderPass.h"
#include "Scene.h"
#include "RendererBatch.h"
#include "Shaders/ShadowShader.h"
#include "Components/PerspectiveCamera.h"
#include "VulkanWrapper/Framebuffer.h"
#include "VulkanWrapper/SwapChain.h"
#include "VulkanWrapper/CommandBuffer.h"
#include "VulkanWrapper/Pipeline.h"
#include "MaterialFactory.h"
#include "Material.h"
#include "Core/Components/Mesh.h"
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/euler_angles.hpp>
using namespace Core;

Core::ShadowRenderPass::ShadowRenderPass(Device& device, Scene& scene, SwapChain& swapChain, RenderTarget* depthRenderTarget)
	:RenderPass(device), _scene(scene), _shadowMap(depthRenderTarget)
{
	_directionalLight.View = lookAt(
		vec3(-2.0f, 2.0f, 2.0f),
		vec3(0.0f, 0.0f, 0.0f),
		vec3(0.0f, 0.0f, 1.0f));

	auto extent = swapChain.GetSwapChainExtent();
	_directionalLight.Perspective = glm::perspective(
		radians(60.0f),
		extent.width / (float)extent.height,
		0.1f, 10.0f);
	_directionalLight.Perspective[1][1] *= -1;

	_shadowBuffer.Projection = _directionalLight.Perspective * _directionalLight.View;

	_material = MaterialFactory::CreateMaterial(device, "Assets/Materials/Shadow.json");

	CreateDepthAttachment(depthRenderTarget, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
	CreateRenderPass();

	_framebuffer = new Framebuffer(device, swapChain, *this);

	auto& rasterization = _pipelineState->GetRasterizationStateCreateInfo();
	rasterization.depthBiasEnable = VK_TRUE;
	rasterization.depthBiasSlopeFactor = 1.5f;
}

Core::ShadowRenderPass::~ShadowRenderPass()
{
	delete(_batch);
}

void Core::ShadowRenderPass::Prepare()
{
	_batch = new RendererBatch(_device, _material->GetShader(), *this, *_pipelineState);
	
	auto meshes = _scene.GetComponents<Core::Mesh>();
	for (auto&& mesh : meshes)
	{
		_batch->Add(*mesh, *_material);
	}
}

void Core::ShadowRenderPass::Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex)
{
	UpdateGUI();

	_shadowMap->TransitionImageLayout(commandBuffer,
		0, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

	auto framebuffer = _framebuffer->GetHandle(imageIndex);
	auto renderPassBeginInfo = CreateRenderPassBeginInfo(framebuffer, _framebuffer->GetExtent());
	commandBuffer.BeginRenderPass(renderPassBeginInfo);

	_material->SetBuffer(currentFrame, 0, &_directionalLight);

	_batch->Draw(commandBuffer, currentFrame);

	commandBuffer.EndRenderPass();
}

void Core::ShadowRenderPass::UpdateGUI()
{
	static float radian = 3.14159f / 180.f;

	vec3 skew;
	vec4 perspective;
	vec3 scale;
	quat rotation;
	vec3 translation;
	decompose(_directionalLight.View, scale, rotation, translation, skew, perspective);

	//quat to degree.
	glm::vec3 euler = glm::eulerAngles(rotation) / radian;

	ImGui::Begin("Directional Light");

	bool x = ImGui::SliderFloat("x", &euler.x, -360.0f, 360.0f);
	bool y = ImGui::SliderFloat("y", &euler.y, -360.0f, 360.0f);
	bool z = ImGui::SliderFloat("z", &euler.z, -360.0f, 360.0f);

	ImGui::End();

	if (x || y || z)
	{
		vec3 degreeToRadian = euler * radian;
		quat newRotation = glm::quat(degreeToRadian);

		_directionalLight.View = translate(glm::mat4(1.0), translation) *
			glm::mat4_cast(newRotation) *
			glm::scale(glm::mat4(1.0), scale);

		_shadowBuffer.Projection = _directionalLight.Perspective * _directionalLight.View;
	}
}
