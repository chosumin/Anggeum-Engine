#include "stdafx.h"
#include "ShadowPass.h"
#include "Foundation/Scene.h"
#include "Foundation/Entity.h"
#include "Components/Light.h"
#include "Components/Mesh.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Material.h"
#include "Graphics/ResourceCache.h"
using namespace Core;

Core::ShadowPass::ShadowPass(Device& device, WorkerThreadManager& workerThreadManager, 
	Scene& scene, SwapChain& swapChain, Texture* depthRenderTarget, TransformBatch& transformBatch)
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

	_rendererBatches = make_unique<RendererBatches>(transformBatch);
}

Core::ShadowPass::~ShadowPass()
{
}

void Core::ShadowPass::Prepare()
{
	auto meshes = _scene.GetComponents<Core::Mesh>();
	_rendererBatches->PrepareSingleBatch(_device, _material, *_renderPass, *_pipelineState, meshes);
}

void Core::ShadowPass::Draw(RenderFrame& renderFrame, uint32_t imageIndex)
{
	auto& commandBuffer = renderFrame.GetCommandBuffer();

	UpdateGUI();

	commandBuffer.TransitionImageLayout(*_shadowMap->GetImage().lock(),
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

	commandBuffer.SetViewportAndScissor(_framebuffer->GetExtent());

	auto renderPassBeginInfo = _renderPass->CreateRenderPassBeginInfo(*_framebuffer, imageIndex);
	commandBuffer.BeginRenderPass(renderPassBeginInfo);

	_rendererBatches->Draw(renderFrame, commandBuffer,
	[&](shared_ptr<Shader> shader)
	{
		renderFrame.SetShaderUniformBuffer(*shader, 0, &_directionalLight);
	},
	[&](shared_ptr<Material> sharedMaterial, shared_ptr<SubMesh> subMesh)
	{
	});

	commandBuffer.EndRenderPass();
}

void Core::ShadowPass::UpdateGUI()
{
}
