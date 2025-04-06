#include "stdafx.h"
#include "ShadowRenderPass.h"
#include "Scene.h"
#include "RendererBatch.h"
#include "Entity.h"
#include "Components/Mesh.h"
#include "Components/Light.h"
#include "VulkanWrapper/SwapChain.h"
#include "VulkanWrapper/CommandBuffer.h"
#include "VulkanWrapper/Pipeline.h"
#include "MaterialFactory.h"
#include "Material.h"
using namespace Core;

Core::ShadowRenderPass::ShadowRenderPass(Device& device, Scene& scene, SwapChain& swapChain, Texture* depthRenderTarget)
	:RenderPass(device), _scene(scene), _shadowMap(depthRenderTarget)
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

	_material = MaterialFactory::CreateMaterial(device, "Assets/Materials/Shadow.json");

	CreateDepthAttachment(depthRenderTarget, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
	CreateRenderPass();

	CreateFrameBuffer(swapChain);

	auto& rasterization = _pipelineState->GetRasterizationStateCreateInfo();
	rasterization.depthBiasEnable = VK_TRUE;
	rasterization.depthBiasSlopeFactor = 1.5f;
}

Core::ShadowRenderPass::~ShadowRenderPass()
{
	delete(_batch);
}

void Core::ShadowRenderPass::Prepare()
{
	_batch = new RendererBatch(_device, _material->GetShader(), *this, *_pipelineState);
	
	auto meshes = _scene.GetComponents<Core::Mesh>();
	for (auto&& mesh : meshes)
	{
		_batch->Add(*mesh, *_material);
	}
}

void Core::ShadowRenderPass::Draw(CommandBuffer& commandBuffer, uint32_t currentFrame, uint32_t imageIndex)
{
	UpdateGUI();

	commandBuffer.TransitionImageLayout(*_shadowMap->GetImage(),
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

	commandBuffer.SetViewportAndScissor(GetBufferExtent2D());

	auto renderPassBeginInfo = CreateRenderPassBeginInfo(imageIndex);
	commandBuffer.BeginRenderPass(renderPassBeginInfo);

	_material->SetBuffer(currentFrame, 0, &_directionalLight);

	_batch->Draw(commandBuffer, currentFrame);

	commandBuffer.EndRenderPass();
}

void Core::ShadowRenderPass::UpdateGUI()
{
}
