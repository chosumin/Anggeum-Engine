#include "stdafx.h"
#include "DepthPrePass.h"
#include "Foundation/Scene.h"
#include "Foundation/Entity.h"
#include "Components/Mesh.h"
#include "Components/PerspectiveCamera.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/Material.h"
#include "Graphics/ResourceCache.h"

Core::DepthPrePass::DepthPrePass(Device& device, WorkerThreadManager& workerThreadManager, Scene& scene, SwapChain& swapChain, Texture* depthRenderTarget)
	:RendererPass(device, workerThreadManager), _scene(scene), _batch(nullptr),
	_depthMap(depthRenderTarget)
{
	_material = device.GetResourceCache().RequestMaterial("pre depth", "Shadow");

	_renderPass->CreateDepthAttachment(depthRenderTarget, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
	_renderPass->CreateRenderPass();

	CreateFrameBuffer(swapChain);
}

Core::DepthPrePass::~DepthPrePass()
{
	delete(_batch);
}

void Core::DepthPrePass::Prepare()
{
	_batch = new RendererBatch(_device, _material->GetShader(), *_renderPass, *_pipelineState);

	auto meshes = _scene.GetComponents<Core::Mesh>();
	for (auto&& mesh : meshes)
	{
		_batch->Add(*mesh, _material);
	}
}

void Core::DepthPrePass::Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex)
{
	commandBuffer.TransitionImageLayout(*_depthMap->GetImage().lock(),
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

	commandBuffer.SetViewportAndScissor(_framebuffer->GetExtent());

	auto renderPassBeginInfo = _renderPass->CreateRenderPassBeginInfo(*_framebuffer, imageIndex);
	commandBuffer.BeginRenderPass(renderPassBeginInfo);

	PerspectiveCamera* camera = _scene.GetMainCamera();
	_material->SetBuffer(currentFrame, 0, &camera->Matrices);

	_batch->Draw(commandBuffer, currentFrame);

	commandBuffer.EndRenderPass();
}
