#include "stdafx.h"
#include "GeometryRenderPass.h"
#include "Scene.h"
#include "Components/PerspectiveCamera.h"
#include "Components/Light.h"
#include "Components/Mesh.h"
#include "VulkanWrapper/CommandBuffer.h"
#include "VulkanWrapper/SwapChain.h"
#include "VulkanWrapper/CommandPool.h"
#include "VulkanWrapper/Pipeline.h"
#include "VulkanWrapper/Shader.h"
#include "Material.h"
#include "RendererBatch.h"
#include "SubMesh.h"
#include "Component.h"
#include "SkyPregenerationRenderPass.h"

namespace Core
{
	GeometryRenderPass::GeometryRenderPass(Device& device, Scene& scene, SwapChain& swapChain,
		Texture* colorRenderTarget, Texture* depthRenderTarget, 
		Texture* shadowRenderTarget, 
		Texture* pregenerationSky, Texture* irradianceCubemap)
		:RenderPass(device), _scene(scene), _shadowRenderTarget(shadowRenderTarget),
		_irradianceCubemap(irradianceCubemap), _skyboxPipeline(nullptr)
	{
		auto extent = swapChain.GetSwapChainExtent();
		CreateColorAttachment(colorRenderTarget,
			VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
		CreateDepthAttachment(depthRenderTarget,
			VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE);
		CreateRenderPass();
		CreateFrameBuffer(swapChain);

		auto& multiSampling = _pipelineState->GetMultisampleStateCreateInfo();
		multiSampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;

		PreparePregenerationSkybox(pregenerationSky, irradianceCubemap);
	}

	GeometryRenderPass::~GeometryRenderPass()
	{
		for (auto&& batch : _batches)
		{
			delete(batch.second);
		}

		_batches.clear();

		delete(_skyboxPipeline);
	}

	void GeometryRenderPass::Prepare()
	{
		auto meshes = _scene.GetComponents<Core::Mesh>();
		for (auto&& mesh : meshes)
		{
			auto materials = mesh->GetMaterials();
			for (size_t i = 0; i < materials.size(); ++i)
			{
				auto& shader = materials[i]->GetShader();

				if (shader.GetPass() == "Geometry")
				{
					auto key = shader.GetType();
					auto batch = _batches[key];
					if (batch == nullptr)
					{
						batch = new RendererBatch(_device, shader, *this, *_pipelineState);
						_batches[key] = batch;
					}

					batch->Add(*mesh);
				}
			}
		}
	}

	void GeometryRenderPass::Draw(CommandBuffer& commandBuffer,
		uint32_t currentFrame, uint32_t imageIndex)
	{
		UpdateGUI();
		UpdateLightBuffer();

		commandBuffer.TransitionImageLayout(*_shadowRenderTarget->GetImage(),
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		commandBuffer.SetViewportAndScissor(GetBufferExtent2D());

		auto renderPassBeginInfo = CreateRenderPassBeginInfo(imageIndex);
		commandBuffer.BeginRenderPass(renderPassBeginInfo);

		PerspectiveCamera* camera = _scene.GetMainCamera();

		for (auto&& batch : _batches)
		{
			for (auto&& material : batch.second->Materials)
			{
				material.second->SetBuffer(currentFrame, 0, &camera->Matrices);
				material.second->SetBuffer(4, _shadowRenderTarget);
				material.second->SetBuffer(currentFrame, 5, &_shadowBuffer->Projection);
				material.second->SetBuffer(currentFrame, 7, &_lightBuffer);
				material.second->SetBuffer(currentFrame);
			}

			batch.second->Draw(commandBuffer, currentFrame);
		}

		DrawSkybox(commandBuffer, currentFrame);

		commandBuffer.EndRenderPass();
	}

	void GeometryRenderPass::PreparePregenerationSkybox(Texture* pregenerationSky, Texture* environmentCubemap)
	{
		auto pregenerationSkybox = new SkyPregenerationRenderPass(_device, _scene, pregenerationSky, environmentCubemap);

		pregenerationSkybox->Prepare();

		auto& singleCommand = _device.BeginSingleTimeCommands();

		pregenerationSkybox->Draw(singleCommand, 0, 0);
		
		_device.EndSingleTimeCommands(singleCommand);

		delete(pregenerationSkybox);
	}

	void GeometryRenderPass::DrawSkybox(CommandBuffer& commandBuffer, uint32_t currentFrame)
	{
		PerspectiveCamera* camera = _scene.GetMainCamera();

		auto meshes = _scene.GetComponents<Core::Mesh>();

		auto it = find_if(meshes.begin(), meshes.end(), [](Mesh* mesh) 
		{
			auto material = mesh->GetMaterials()[0];
			auto& shader = material->GetShader();
			return shader.GetPass() == "Skybox";
		});

		if (it != meshes.end())
		{
			auto skybox = *it;
			auto material = skybox->GetMaterials()[0];
			auto subMesh = skybox->GetSubMeshes()[0];
			auto& shader = material->GetShader();

			if (_skyboxPipeline == nullptr)
			{
				auto pipelineState = *_pipelineState;
				auto& depthInfo = pipelineState.GetDepthStencilStateCreateInfo();
				depthInfo.depthWriteEnable = VK_FALSE;

				auto& rasterizationInfo = pipelineState.GetRasterizationStateCreateInfo();
				rasterizationInfo.cullMode = VK_CULL_MODE_FRONT_BIT;

				_skyboxPipeline = new Pipeline(_device, *this, shader, pipelineState);
			}

			material->SetBuffer(currentFrame, 0, &camera->Matrices);
			material->SetBuffer(1, _irradianceCubemap);

			commandBuffer.BindPipeline(_skyboxPipeline);

			commandBuffer.BindDescriptorSets(
				_skyboxPipeline->GetPipelineBindPoint(), *material, currentFrame);

			auto vertexAttibuteNames = material->GetShader().GetVertexAttirbuteNames();

			commandBuffer.BindVertexBuffers(subMesh->GetVertexBuffers(vertexAttibuteNames), 0);

			commandBuffer.BindIndexBuffer(subMesh->GetIndexBuffer(), subMesh->GetIndexType());

			commandBuffer.DrawIndexed(subMesh->GetIndexCount(), 1);
		}
	}

	void GeometryRenderPass::UpdateGUI()
	{
		auto mainLight = _scene.GetMainLight();

		auto& properties = mainLight->GetProperties();
		auto& transform = mainLight->GetEntity().GetTransform();

		auto& rotation = transform.GetRotation();
		glm::vec3 euler = glm::eulerAngles(rotation);
		euler = glm::degrees(euler);

		ImGui::Begin("Directional Light");

		ImGui::SliderFloat3("Color", &properties.Color[0], 0, 1);
		ImGui::SliderFloat3("Direction", &euler[0], -90.0f, 90.0f);

		ImGui::End();

		transform.SetRotation(euler);
	}

	void GeometryRenderPass::UpdateLightBuffer()
	{
		auto mainLight = _scene.GetMainLight();

		auto& properties = mainLight->GetProperties();
		auto& transform = mainLight->GetEntity().GetTransform();

		LightInfo lightInfo{};
		lightInfo.Position = vec4(transform.GetTranslation(), 
			static_cast<float>(mainLight->GetLightType()));
		lightInfo.Color = vec4(properties.Color, properties.Intensity);

		auto direction = transform.GetRotation() * properties.Direction;
		lightInfo.Direction = 
			vec4(direction, properties.Range);
		lightInfo.Info = vec2(properties.InnerConeAngle, properties.OuterConeAngle);

		_lightBuffer.Light = lightInfo;
	}
}
