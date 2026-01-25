#include "stdafx.h"
#include "GeometryPass.h"
#include "Foundation/Scene.h"
#include "Foundation/Component.h"
#include "Components/PerspectiveCamera.h"
#include "Components/Light.h"
#include "Components/Mesh.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Material.h"
#include "Graphics/SubMesh.h"
#include "PreEnvironmentPass.h"
#include "BrdfLutPass.h"

namespace Core
{
	GeometryPass::GeometryPass(Device& device, WorkerThreadManager& workerThreadManager,
		Scene& scene, SwapChain& swapChain,
		shared_ptr<Texture> colorRenderTarget, shared_ptr<Texture> depthRenderTarget,
		GI& giBuffer,
		shared_ptr<Texture> shadowRenderTarget, 
		shared_ptr<Texture> pregenerationSky, shared_ptr<Texture> irradianceCubemap,
		shared_ptr<Texture> prefilterCubemap, shared_ptr<Texture> brdfLut,
		Buffer* lightVisibilityBuffer, ivec2 tileNums,
		TransformBatch& transformBatch)
		:RendererPass(device, workerThreadManager), _scene(scene), _shadowRenderTarget(shadowRenderTarget),
		_irradianceCubemap(irradianceCubemap), _prefilteredCubemap(prefilterCubemap), 
		_brdfLut(brdfLut), _shadowBuffer(nullptr), _lightBuffer(),
		_skyboxPipeline(nullptr),
		_lightVisibilityBuffer(lightVisibilityBuffer),
		_giBuffer(giBuffer)
	{
		_renderPass->CreateColorAttachment(colorRenderTarget.get(),
			VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
		_renderPass->CreateDepthAttachment(depthRenderTarget.get(),
			VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_DONT_CARE);
		_renderPass->CreateRenderPass();

		CreateFrameBuffer(swapChain);

		PreparePregenerationSkybox(pregenerationSky.get(), irradianceCubemap.get(), prefilterCubemap.get());

		auto swapChainExtents = swapChain.GetSwapChainExtent();
		_tileInfo.viewportSize = ivec2(swapChainExtents.width, swapChainExtents.height);
		_tileInfo.tileNums = tileNums;

		_rendererBatches = make_unique<RendererBatches>(transformBatch);
	}

	GeometryPass::~GeometryPass()
	{
		delete(_skyboxPipeline);
	}

	void GeometryPass::Prepare()
	{
		auto& multiSampling = _pipelineState->GetMultisampleStateCreateInfo();
		multiSampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;

		auto& depthStencil = _pipelineState->GetDepthStencilStateCreateInfo();
		depthStencil.depthWriteEnable = VK_FALSE;

		auto meshes = _scene.GetComponents<Core::Mesh>();
		_rendererBatches->Prepare(_device, *_renderPass, *_pipelineState, meshes);
	}

	void GeometryPass::Draw(RenderFrame& renderFrame, uint32_t imageIndex)
	{
		auto& commandBuffer = renderFrame.GetCommandBuffer();

		UpdateGUI();
		UpdateLightBuffer();

		commandBuffer.TransitionImageLayout(*_shadowRenderTarget->GetImage().lock(),
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		commandBuffer.SetViewportAndScissor(_framebuffer->GetExtent());

		auto renderPassBeginInfo = 
			_renderPass->CreateRenderPassBeginInfo(*_framebuffer, imageIndex);
		commandBuffer.BeginRenderPass(renderPassBeginInfo);

		PerspectiveCamera* camera = _scene.GetMainCamera();

		_rendererBatches->Draw(renderFrame, commandBuffer,
		[&](shared_ptr<Shader> shader) 
		{
			renderFrame.SetShaderUniformBuffer(*shader, 0, &camera->Matrices);
			renderFrame.SetShaderUniformBuffer(*shader, 3, &_giBuffer);
			renderFrame.SetShaderUniformBuffer(*shader, 4, &_shadowBuffer->Projection);
			renderFrame.SetShaderUniformBuffer(*shader, 5, &_lightBuffer);
			renderFrame.SetShaderStorageBuffer(*shader, 6, _lightVisibilityBuffer);
		},
		[&](shared_ptr<Material> sharedMaterial, shared_ptr<SubMesh> subMesh)
		{
			sharedMaterial->SetPushConstants<TileInfo>(_tileInfo);
			commandBuffer.PushConstants(*sharedMaterial, 0);
		});

		DrawSkybox(renderFrame, commandBuffer);

		commandBuffer.EndRenderPass();
	}

	void GeometryPass::PreparePregenerationSkybox(Texture* pregenerationSky,
		Texture* irradianceCubemap, Texture* prefilterCubemap)
	{
		_timer.tick();

		// PreEnvironmentPass - command buffer allocated from worker thread
		auto preEnvironmentPass = new PreEnvironmentPass(_device, _workerThreadManager, _scene, 
			pregenerationSky, irradianceCubemap, prefilterCubemap);
		auto preEnvironmentJob = new PreEnvironmentJob(_device, *preEnvironmentPass);
		Enqueue(preEnvironmentJob);

		auto brdf = new BrdfLutPass(_device, _workerThreadManager, _brdfLut.get());
		auto brdfJob = new BrdfLutJob(_device, *brdf);
		Enqueue(brdfJob);

		Wait();

		const size_t commandSize = 2;
		vector<VkCommandBuffer> commands(commandSize);
		commands[0] = preEnvironmentJob->commandBuffer->GetHandle();
		commands[1] = brdfJob->commandBuffer->GetHandle();

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = commandSize;
		submitInfo.pCommandBuffers = commands.data();

		VkFenceCreateInfo fence_info{};
		fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fence_info.flags = 0;

		VkFence fence;
		vkCreateFence(_device.GetDevice(), &fence_info, nullptr, &fence);

		vkQueueSubmit(_device.GetGraphicsQueue(), 1, &submitInfo, fence);
		
		auto deltaTime = static_cast<float>(_timer.tick<Core::Timer::Seconds>());
		std::cout << "Generation IBL resources time : " << deltaTime << endl;

		vkWaitForFences(_device.GetDevice(), 1, &fence, VK_TRUE, 100000000000);

		vkDestroyFence(_device.GetDevice(), fence, nullptr);

		delete(brdf);
		delete(brdfJob);
		delete(preEnvironmentPass);
		delete(preEnvironmentJob);
	}

	void GeometryPass::DrawSkybox(RenderFrame& renderFrame, CommandBuffer& commandBuffer)
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

			renderFrame.SetShaderUniformBuffer(shader, 0, &camera->Matrices);

			commandBuffer.BindPipeline(_skyboxPipeline);

			commandBuffer.BindDescriptorSets(
				renderFrame,
				_skyboxPipeline->GetPipelineBindPoint(), material->GetShader());
			commandBuffer.BindDescriptorSets(
				renderFrame,
				_skyboxPipeline->GetPipelineBindPoint(), *material);

			auto vertexAttibuteNames = material->GetShader().GetVertexAttirbuteNames();

			commandBuffer.BindVertexBuffers(subMesh->GetVertexBuffers(vertexAttibuteNames), 0);

			commandBuffer.BindIndexBuffer(subMesh->GetIndexBuffer(), subMesh->GetIndexType());

			commandBuffer.DrawIndexed(subMesh->GetIndexCount(), 1);
		}
	}

	void GeometryPass::UpdateGUI()
	{
	}

	void GeometryPass::UpdateLightBuffer()
	{
		auto lights = _scene.GetComponents<Light>();

		uint32_t size = std::min((uint32_t)lights.size(), (uint32_t)MAX_FORWARD_LIGHT_COUNT);
		for (uint32_t i = 0; i < size; ++i)
		{
			auto light = lights[i];

			auto& properties = light->GetProperties();
			auto& transform = light->GetEntity().GetTransform();

			LightInfo lightInfo{};
			lightInfo.Position = vec4(transform.GetTranslation(),
				static_cast<float>(light->GetLightType()));
			lightInfo.Color = vec4(properties.Color, properties.Intensity);

			auto direction = transform.GetRotation() * properties.Direction;
			lightInfo.Direction =
				vec4(direction, properties.Range);
			lightInfo.Info = vec2(properties.InnerConeAngle, properties.OuterConeAngle);

			_lightBuffer.Light[i] = lightInfo;
		}

		_lightBuffer.Count = size;
	}
}
