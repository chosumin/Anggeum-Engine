#include "stdafx.h"
#include "PreEnvironmentPass.h"
#include "Foundation/Scene.h"
#include "Foundation/Entity.h"
#include "Components/Mesh.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/SubMesh.h"
#include "Graphics/Material.h"
#include "Graphics/ResourceCache.h"
#include "Utils/Utility.h"
using namespace Core;

#define PI 3.1415926535897932384626433832795

Core::PreEnvironmentPass::PreEnvironmentPass(Device& device, 
	WorkerThreadManager& workerThreadManager, Scene& scene,
	Texture* offscreen, Texture* irradianceCubemap, Texture* prefilteredCubemap)
	:RendererPass(device, workerThreadManager), _scene(scene), _colorRenderTarget(offscreen),
	_irradianceCubemap(irradianceCubemap), _prefilteredCubemap(prefilteredCubemap),
	_irradianceMaterial(device.GetResourceCache().RequestMaterial("irradiance", "Irradiance")),
	_prefilteredMaterial(device.GetResourceCache().RequestMaterial("prefiltered", "Prefiltered"))
{
	_renderPass->CreateColorAttachment(offscreen, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
	_renderPass->CreateRenderPass();

	CreateFrameBuffer(offscreen->GetImage().lock().get());
}

Core::PreEnvironmentPass::~PreEnvironmentPass()
{
	delete(_irradiancePipeline);
	delete(_prefilteredPipeline);
}

void Core::PreEnvironmentPass::Prepare()
{
	auto meshes = _scene.GetComponents<Core::Mesh>();

	auto it = find_if(meshes.begin(), meshes.end(), [](Mesh* mesh)
	{
		auto material = mesh->GetMaterials()[0];
		auto& shader = material->GetShader();
		return shader.GetPass() == "Skybox";
	});

	shared_ptr<Texture> skyCubemap;

	if (it != meshes.end())
	{
		auto skybox = *it;
		
		_sky = skybox->GetSubMeshes()[0];
		auto material = skybox->GetMaterials()[0];
		skyCubemap = material->GetTexture(0, 1);

		auto pipelineState = *_pipelineState;

		auto& depthInfo = pipelineState.GetDepthStencilStateCreateInfo();
		depthInfo.depthWriteEnable = VK_FALSE;
		depthInfo.depthTestEnable = VK_FALSE;

		_irradiancePipeline = new Pipeline(_device, *_renderPass, _irradianceMaterial->GetShader(), pipelineState);
		_prefilteredPipeline = new Pipeline(_device, *_renderPass, _prefilteredMaterial->GetShader(), pipelineState);
	}

	_mvpMatrices = {
		glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
		glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
		glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
		glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
		glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
		glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
	};

	_delta.Phi = (2.0f * float(PI)) / 180.0f;
	_delta.Theta = (0.5f * float(PI)) / 64.0f;

	_irradianceMaterial->SetBuffer(0, 0, skyCubemap);
	_prefilteredMaterial->SetBuffer(0, 0, skyCubemap);
}

void Core::PreEnvironmentPass::Draw(RenderFrame& renderFrame, uint32_t frameIndex, uint32_t imageIndex)
{
	auto& commandBuffer = renderFrame.GetCommandBuffer();
	
	DrawIrradiance(commandBuffer, frameIndex, imageIndex);
	DrawPrefiltered(commandBuffer, frameIndex, imageIndex);
}

void Core::PreEnvironmentPass::DrawIrradiance(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex)
{
	commandBuffer.TransitionImageLayout(*_irradianceCubemap->GetImage().lock(),
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	uint32_t mipLevels = _irradianceCubemap->GetMipLevels();
	uint32_t layers = _irradianceCubemap->GetLayers();

	auto extent = _framebuffer->GetExtent();

	for (uint32_t m = 0; m < mipLevels; ++m)
	{
		for (uint32_t layer = 0; layer < layers; ++layer)
		{
			VkExtent2D mipExtent;
			mipExtent.width = static_cast<uint32_t>(extent.width * pow(0.5f, m));
			mipExtent.height = static_cast<uint32_t>(extent.height * pow(0.5f, m));

			commandBuffer.SetViewportAndScissor(mipExtent);

			auto beginInfo = 
				_renderPass->CreateRenderPassBeginInfo(*_framebuffer, imageIndex);
			commandBuffer.BeginRenderPass(beginInfo);

			_irradianceMaterial->SetPushConstants<mat4>(glm::perspective((float)(PI / 2.0), 1.0f, 0.1f, 512.0f) * _mvpMatrices[layer]);
			commandBuffer.PushConstants(*_irradianceMaterial, 0);

			_irradianceMaterial->SetPushConstants<IrradianceDelta>(_delta);
			commandBuffer.PushConstants(*_irradianceMaterial, 1);

			commandBuffer.BindPipeline(_irradiancePipeline);

			commandBuffer.BindDescriptorSets(
				_irradiancePipeline->GetPipelineBindPoint(), *_irradianceMaterial, currentFrame);

			auto vertexAttibuteNames = _irradianceMaterial->GetShader().GetVertexAttirbuteNames();

			commandBuffer.BindVertexBuffers(_sky->GetVertexBuffers(vertexAttibuteNames), 0);

			commandBuffer.BindIndexBuffer(_sky->GetIndexBuffer(), _sky->GetIndexType());

			commandBuffer.DrawIndexed(_sky->GetIndexCount(), 1);

			commandBuffer.EndRenderPass();

			commandBuffer.TransitionImageLayout(*_colorRenderTarget->GetImage().lock(),
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

			commandBuffer.CopyImage(*_colorRenderTarget->GetImage().lock(), *_irradianceCubemap->GetImage().lock(), 0, 0, m, layer);

			commandBuffer.TransitionImageLayout(*_colorRenderTarget->GetImage().lock(),
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		}
	}

	commandBuffer.TransitionImageLayout(*_irradianceCubemap->GetImage().lock(),
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void Core::PreEnvironmentPass::DrawPrefiltered(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex)
{
	commandBuffer.TransitionImageLayout(*_prefilteredCubemap->GetImage().lock(),
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	uint32_t mipLevels = _prefilteredCubemap->GetMipLevels();
	uint32_t layers = _prefilteredCubemap->GetLayers();

	auto extent = _framebuffer->GetExtent();

	for (uint32_t m = 0; m < mipLevels; ++m)
	{
		for (uint32_t layer = 0; layer < layers; ++layer)
		{
			VkExtent2D mipExtent;
			mipExtent.width = static_cast<uint32_t>(extent.width * pow(0.5f, m));
			mipExtent.height = static_cast<uint32_t>(extent.height * pow(0.5f, m));

			commandBuffer.SetViewportAndScissor(mipExtent);

			auto beginInfo =
				_renderPass->CreateRenderPassBeginInfo(*_framebuffer, imageIndex);
			commandBuffer.BeginRenderPass(beginInfo);

			_prefilteredMaterial->SetPushConstants<mat4>(glm::perspective((float)(PI / 2.0), 1.0f, 0.1f, 512.0f) * _mvpMatrices[layer]);
			commandBuffer.PushConstants(*_prefilteredMaterial, 0);

			_prefilterEnv.Roughness = (float)m / (float)(mipLevels - 1);
			_prefilteredMaterial->SetPushConstants<PrefilterEnv>(_prefilterEnv);
			commandBuffer.PushConstants(*_prefilteredMaterial, 1);

			commandBuffer.BindPipeline(_prefilteredPipeline);

			commandBuffer.BindDescriptorSets(_prefilteredPipeline->GetPipelineBindPoint(),
				*_prefilteredMaterial, currentFrame);

			auto vertexAttibuteNames = _prefilteredMaterial->GetShader().GetVertexAttirbuteNames();

			commandBuffer.BindVertexBuffers(_sky->GetVertexBuffers(vertexAttibuteNames), 0);

			commandBuffer.BindIndexBuffer(_sky->GetIndexBuffer(), _sky->GetIndexType());

			commandBuffer.DrawIndexed(_sky->GetIndexCount(), 1);

			commandBuffer.EndRenderPass();

			commandBuffer.TransitionImageLayout(*_colorRenderTarget->GetImage().lock(),
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

			commandBuffer.CopyImage(*_colorRenderTarget->GetImage().lock(), *_prefilteredCubemap->GetImage().lock(), 0, 0, m, layer);

			commandBuffer.TransitionImageLayout(*_colorRenderTarget->GetImage().lock(),
				VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		}
	}

	commandBuffer.TransitionImageLayout(*_prefilteredCubemap->GetImage().lock(),
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

Core::PreEnvironmentJob::PreEnvironmentJob(Device& device, PreEnvironmentPass& pass)
	: Job(JobType::GRAPHICS_PRIMARY)
	, _pass(pass)
	, _tempRenderFrame(device)
{
	_pass.Prepare();
}

Core::PreEnvironmentJob::~PreEnvironmentJob()
{
}

void Core::PreEnvironmentJob::Execute()
{
	_tempRenderFrame.SetCommandBuffer(commandBuffer);

	_pass.Draw(_tempRenderFrame, 0, 0);
	
	status = JobStatus::COMPLETE;
}
