#include "stdafx.h"
#include "DepthPrePass.h"
#include "Foundation/Scene.h"
#include "Foundation/Entity.h"
#include "Components/PerspectiveCamera.h"
#include "Components/Mesh.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Material.h"
#include "Graphics/ResourceCache.h"

Core::DepthPrePass::DepthPrePass(Device& device, WorkerThreadManager& workerThreadManager, Scene& scene, SwapChain& swapChain, Texture* depthRenderTarget, TransformBatch& transformBatch)
	:RendererPass(device, workerThreadManager), _scene(scene)
{
	_renderPass->CreateDepthAttachment(depthRenderTarget, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
	_renderPass->CreateRenderPass();

	CreateFrameBuffer(swapChain);

	_rendererBatches = make_unique<RendererBatches>(transformBatch);
}

Core::DepthPrePass::~DepthPrePass()
{
}

void Core::DepthPrePass::Prepare()
{
	_material = _device.GetResourceCache().RequestMaterial("depth prepass", "Depth");

	auto& multiSampling = _pipelineState->GetMultisampleStateCreateInfo();
	multiSampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;

	auto meshes = _scene.GetComponents<Core::Mesh>();
	_rendererBatches->PrepareSingleBatch(_device, _material, *_renderPass, *_pipelineState, meshes);

	if (_device.IsGpuDrivenRenderingEnabled())
		_rendererBatches->PrepareIndirectCommands(_device, false);
}

void Core::DepthPrePass::Draw(RenderFrame& renderFrame, uint32_t imageIndex)
{
	// Get command buffer from RenderFrame
	auto& commandBuffer = renderFrame.GetCommandBuffer();
	
	PerspectiveCamera* camera = _scene.GetMainCamera();

	commandBuffer.SetViewportAndScissor(_framebuffer->GetExtent());

	auto renderPassBeginInfo = _renderPass->CreateRenderPassBeginInfo(*_framebuffer, imageIndex);
	commandBuffer.BeginRenderPass(renderPassBeginInfo);

	if (_device.IsGpuDrivenRenderingEnabled())
	{
		_rendererBatches->DrawIndirect(renderFrame, commandBuffer,
		[&](shared_ptr<Shader> shader)
		{
			renderFrame.SetShaderUniformBuffer(*shader, 0, &camera->Matrices);
		},
		[&](shared_ptr<Material> sharedMaterial)
		{
		});
	}
	else
	{
		_rendererBatches->Draw(renderFrame, commandBuffer,
		[&](shared_ptr<Shader> shader)
		{
			renderFrame.SetShaderUniformBuffer(*shader, 0, &camera->Matrices);
		},
		[&](shared_ptr<Material> sharedMaterial, shared_ptr<SubMesh> subMesh)
		{
		});
	}

	commandBuffer.EndRenderPass();
}
