#include "stdafx.h"
#include "ShadowRenderPass.h"
#include "Scene.h"
#include "SwapChain.h"
#include "InstanceData.h"
#include "PerspectiveCamera.h"
#include "Entity.h"
#include "MeshRenderer.h"
#include "RendererBatch.h"
#include "ShadowShader.h"
#include "Framebuffer.h"
#include "CommandBuffer.h"
#include "Pipeline.h"
#include "MaterialFactory.h"
#include "Material.h"
#include "Mesh.h"
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

	_material = MaterialFactory::CreateMaterial(device, MaterialType::SHADOW);

	CreateDepthAttachment(depthRenderTarget, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
	CreateRenderPass();

	_framebuffer = new Framebuffer(device, swapChain, *this);
	_instanceBuffer = new InstanceBuffer(device);
}

Core::ShadowRenderPass::~ShadowRenderPass()
{
	for (auto&& batch : _batches)
	{
		delete(batch.second);
	}

	_batches.clear();

	delete(_instanceBuffer);
}

void Core::ShadowRenderPass::Prepare()
{
	auto meshRenderers = _scene.GetComponents<Core::MeshRenderer>();
	for (auto&& meshRenderer : meshRenderers)
	{
		auto key = _material->GetShader().GetType();
		auto batch = _batches[key];
		if (batch == nullptr)
		{
			batch = new RendererBatch(_device, _material->GetShader(), *this, _shadowMap->SampleCount);
			_batches[key] = batch;
		}

		batch->Add(*meshRenderer);
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

	for (auto&& batch : _batches)
	{
		auto& pipeline = batch.second->Pipeline;
		commandBuffer.BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipeline->GetGraphicsPipeline());

		auto& materials = batch.second->Materials;
		for (auto&& material : materials)
		{
			auto& meshBatches =
				batch.second->MeshBatches[material.first];

			commandBuffer.BindDescriptorSets(
				VK_PIPELINE_BIND_POINT_GRAPHICS, *_material, currentFrame);

			auto vertexAttibuteNames = _material->GetShader().GetVertexAttirbuteNames();

			for (auto&& meshBatch : meshBatches)
			{
				RendererBatch::Sort(material.first);

				commandBuffer.Flush(*material.second);

				auto& meshRenderers = meshBatch.second;
				for (size_t i = 0; i < meshRenderers.size(); ++i)
				{
					auto& entity = meshRenderers[i]->GetEntity();
					_instanceBuffer->SetBuffer(i, entity.GetTransform());
				}
				_instanceBuffer->Copy();

				auto& mesh = meshBatch.second[0]->GetMesh();
				commandBuffer.BindVertexBuffers(mesh.GetVertexBuffers(vertexAttibuteNames), 0);
				commandBuffer.BindVertexBuffers(
					_instanceBuffer->GetBuffer(), 1);

				commandBuffer.BindIndexBuffer(mesh.GetIndexBuffer(), VK_INDEX_TYPE_UINT32);

				commandBuffer.DrawIndexed(mesh.GetIndexCount(), static_cast<uint32_t>(meshRenderers.size()));
			}
		}
	}

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
