#include "stdafx.h"
#include "ShadowPass.h"
#include "Foundation/Scene.h"
#include "Foundation/Entity.h"
#include "Graphics/RendererBatch.h"
#include "Components/Mesh.h"
#include "Components/Light.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Material.h"
#include "Graphics/ResourceCache.h"
using namespace Core;

Core::ShadowPass::ShadowPass(Device& device, WorkerThreadManager& workerThreadManager, 
	Scene& scene, SwapChain& swapChain, Texture* depthRenderTarget)
	: RendererPass(device, workerThreadManager), _scene(scene), _shadowMap(depthRenderTarget)
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

	//TODO : remove _shadowBuffer  
	_shadowBuffer.Projection = _directionalLight.Perspective * _directionalLight.View;

	_material = device.GetResourceCache().RequestMaterial("shadow material", "Shadow");

	_renderPass->CreateDepthAttachment(depthRenderTarget, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
	_renderPass->CreateRenderPass();

	CreateFrameBuffer(swapChain);

	auto& rasterization = _pipelineState->GetRasterizationStateCreateInfo();
	rasterization.depthBiasEnable = VK_TRUE;
	rasterization.depthBiasSlopeFactor = 1.5f;
}

Core::ShadowPass::~ShadowPass()
{
}

void Core::ShadowPass::Prepare()
{
	_rendererBatches = make_unique<RendererBatches>();
	_rendererBatches->PrepareSingleBatch(_device, _material, *_renderPass, *_pipelineState, _scene);
}

void Core::ShadowPass::Draw(CommandBuffer& commandBuffer, CommandBuffer& computeBuffer, uint32_t currentFrame, uint32_t imageIndex)
{
	UpdateGUI();

	commandBuffer.TransitionImageLayout(*_shadowMap->GetImage().lock(),
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

	commandBuffer.SetViewportAndScissor(_framebuffer->GetExtent());

	auto renderPassBeginInfo = _renderPass->CreateRenderPassBeginInfo(*_framebuffer, imageIndex);
	commandBuffer.BeginRenderPass(renderPassBeginInfo);

	_rendererBatches->Draw(commandBuffer, currentFrame,
	[&](shared_ptr<Material> material)
	{
		material->SetBuffer(currentFrame, 0, &_directionalLight);
	},
	[&](shared_ptr<Material> sharedMaterial, shared_ptr<SubMesh> subMesh)
	{
		auto vertexAttibuteNames = sharedMaterial->GetShader().GetVertexAttirbuteNames();

		commandBuffer.BindVertexBuffers(subMesh->GetVertexBuffers(vertexAttibuteNames), 0);

		commandBuffer.BindIndexBuffer(subMesh->GetIndexBuffer(), subMesh->GetIndexType());

		commandBuffer.DrawIndexed(subMesh->GetIndexCount(), static_cast<uint32_t>(transforms.size()));
	});

	commandBuffer.EndRenderPass();
}

void Core::ShadowPass::UpdateGUI()
{
}
