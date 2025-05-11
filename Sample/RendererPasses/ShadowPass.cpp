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
	: RendererPass(device, workerThreadManager), _scene(scene), _shadowMap(depthRenderTarget), _batch(nullptr)
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
	delete(_batch);
}

void Core::ShadowPass::Prepare()
{
	_batch = new RendererBatch(_device, _material->GetShader(), *_renderPass, *_pipelineState);
	
	auto meshes = _scene.GetComponents<Core::Mesh>();
	for (auto&& mesh : meshes)
	{
		_batch->Add(*mesh, _material);
	}
}

void Core::ShadowPass::Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex)
{
	UpdateGUI();

	commandBuffer.TransitionImageLayout(*_shadowMap->GetImage().lock(),
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

	commandBuffer.SetViewportAndScissor(_framebuffer->GetExtent());

	auto renderPassBeginInfo = _renderPass->CreateRenderPassBeginInfo(*_framebuffer, imageIndex);
	commandBuffer.BeginRenderPass(renderPassBeginInfo);

	_material->SetBuffer(currentFrame, 0, &_directionalLight);

	_batch->Draw(commandBuffer, currentFrame);

	commandBuffer.EndRenderPass();
}

void Core::ShadowPass::UpdateGUI()
{
}
