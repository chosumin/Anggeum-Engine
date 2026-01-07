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
	:RendererPass(device, workerThreadManager), _scene(scene)
{
	_renderPass->CreateDepthAttachment(depthRenderTarget, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
	_renderPass->CreateRenderPass();

	CreateFrameBuffer(swapChain);
}

Core::DepthPrePass::~DepthPrePass()
{
}

void Core::DepthPrePass::Prepare()
{
	_material = _device.GetResourceCache().RequestMaterial("depth prepass", "Shadow");

	auto& multiSampling = _pipelineState->GetMultisampleStateCreateInfo();
	multiSampling.rasterizationSamples = VK_SAMPLE_COUNT_8_BIT;

	_rendererBatches = make_unique<RendererBatches>();
	_rendererBatches->PrepareSingleBatch(_device, _material, *_renderPass, *_pipelineState, _scene);
}

void Core::DepthPrePass::Draw(CommandBuffer& commandBuffer, CommandBuffer& computeBuffer, uint32_t currentFrame, uint32_t imageIndex)
{
	PerspectiveCamera* camera = _scene.GetMainCamera();

	commandBuffer.SetViewportAndScissor(_framebuffer->GetExtent());

	auto renderPassBeginInfo = _renderPass->CreateRenderPassBeginInfo(*_framebuffer, imageIndex);
	commandBuffer.BeginRenderPass(renderPassBeginInfo);

	_rendererBatches->Draw(commandBuffer, currentFrame,
	[&](shared_ptr<Material> material)
	{
		material->SetBuffer(currentFrame, 0, &camera->Matrices);
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
