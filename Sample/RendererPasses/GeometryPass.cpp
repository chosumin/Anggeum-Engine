#include "stdafx.h"
#include "GeometryPass.h"
#include "Foundation/Scene.h"
#include "Foundation/Component.h"
#include "Components/PerspectiveCamera.h"
#include "Components/Light.h"
#include "Components/Mesh.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/CommandPool.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Material.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/SubMesh.h"
#include "PreEnvironmentPass.h"
#include "BrdfLutPass.h"
namespace Core
{
	GeometryPass::GeometryPass(Device& device, Scene& scene, SwapChain& swapChain,
		Texture* colorRenderTarget, Texture* depthRenderTarget, 
		Texture* shadowRenderTarget, 
		Texture* pregenerationSky, Texture* irradianceCubemap,
		Texture* prefilterCubemap, Texture* brdfLut)
		:RendererPass(device), _scene(scene), _shadowRenderTarget(shadowRenderTarget),
		_irradianceCubemap(irradianceCubemap), _prefilteredCubemap(prefilterCubemap), 
		_brdfLut(brdfLut), _shadowBuffer(nullptr), _lightBuffer(),
		_skyboxPipeline(nullptr)
	{
		_renderPass->CreateColorAttachment(colorRenderTarget,
			VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
		_renderPass->CreateDepthAttachment(depthRenderTarget,
			VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE);
		_renderPass->CreateRenderPass();

		CreateFrameBuffer(swapChain);

		PreparePregenerationSkybox(pregenerationSky, irradianceCubemap, prefilterCubemap);
	}

	GeometryPass::~GeometryPass()
	{
		for (auto&& batch : _batches)
		{
			delete(batch.second);
		}

		_batches.clear();

		delete(_skyboxPipeline);
	}

	void GeometryPass::Prepare()
	{
		auto& multiSampling = _pipelineState->GetMultisampleStateCreateInfo();
		multiSampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;

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
						batch = new RendererBatch(_device, shader, *_renderPass, *_pipelineState);
						_batches[key] = batch;
					}

					batch->Add(*mesh);
				}
			}
		}
	}

	void GeometryPass::Draw(CommandBuffer& commandBuffer,
		uint32_t currentFrame, uint32_t imageIndex)
	{
		UpdateGUI();
		UpdateLightBuffer();

		commandBuffer.TransitionImageLayout(*_shadowRenderTarget->GetImage(),
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		commandBuffer.SetViewportAndScissor(_framebuffer->GetExtent());

		auto renderPassBeginInfo = 
			_renderPass->CreateRenderPassBeginInfo(*_framebuffer, imageIndex);
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
				material.second->SetBuffer(8, _irradianceCubemap);
				material.second->SetBuffer(9, _prefilteredCubemap);
				material.second->SetBuffer(10, _brdfLut);

				material.second->SetBuffer(currentFrame);
			}

			batch.second->Draw(commandBuffer, currentFrame);
		}

		DrawSkybox(commandBuffer, currentFrame);

		commandBuffer.EndRenderPass();
	}

	void GeometryPass::PreparePregenerationSkybox(Texture* pregenerationSky,
		Texture* irradianceCubemap, Texture* prefilterCubemap)
	{
		auto preEnvironmentPass = new PreEnvironmentPass(_device, _scene, pregenerationSky, irradianceCubemap, prefilterCubemap);
		auto preEnvironmentJob = new PreEnvironmentJob(*preEnvironmentPass);
		Enqueue(preEnvironmentJob);

		auto brdf = new BrdfLutPass(_device, _brdfLut);
		auto brdfJob = new BrdfLutJob(*brdf);
		Enqueue(brdfJob);

		Flush(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

		vector<VkCommandBuffer> commands(_threadsReadyCount);
		for (size_t i = 0; i < _threadsReadyCount; i++)
		{
			commands[i] = _workerThreads[i]->GetCommandBuffer(
				0, VK_COMMAND_BUFFER_LEVEL_PRIMARY).GetHandle();
		}

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = _threadsReadyCount;
		submitInfo.pCommandBuffers = commands.data();

		// Create fence to ensure that the command buffer has finished executing
		VkFenceCreateInfo fence_info{};
		fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fence_info.flags = 0;

		VkFence fence;
		vkCreateFence(_device.GetDevice(), &fence_info, nullptr, &fence);

		vkQueueSubmit(_device.GetGraphicsQueue(), 1, &submitInfo, fence);
		
		auto deltaTime = static_cast<float>(_timer.tick<Core::Timer::Seconds>());
		cout << "Generation IBL resources time : " << deltaTime << endl;

		vkWaitForFences(_device.GetDevice(), 1, &fence, VK_TRUE, 100000000000);

		vkDestroyFence(_device.GetDevice(), fence, nullptr);

		delete(brdf);
		delete(preEnvironmentPass);
	}

	void GeometryPass::DrawSkybox(CommandBuffer& commandBuffer, uint32_t currentFrame)
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

				_skyboxPipeline = new Pipeline(_device, *_renderPass, shader, pipelineState);
			}

			material->SetBuffer(currentFrame, 0, &camera->Matrices);

			commandBuffer.BindPipeline(_skyboxPipeline);

			commandBuffer.BindDescriptorSets(
				_skyboxPipeline->GetPipelineBindPoint(), *material, currentFrame);

			auto vertexAttibuteNames = material->GetShader().GetVertexAttirbuteNames();

			commandBuffer.BindVertexBuffers(subMesh->GetVertexBuffers(vertexAttibuteNames), 0);

			commandBuffer.BindIndexBuffer(subMesh->GetIndexBuffer(), subMesh->GetIndexType());

			commandBuffer.DrawIndexed(subMesh->GetIndexCount(), 1);
		}
	}

	void GeometryPass::UpdateGUI()
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

	void GeometryPass::UpdateLightBuffer()
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
