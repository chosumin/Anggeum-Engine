#include "stdafx.h"
#include "DepthPrePass.h"
#include "Foundation/Scene.h"
#include "Components/PerspectiveCamera.h"
#include "Components/Mesh.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Graphics/Material.h"
#include "Graphics/ResourceCache.h"

using namespace Core;

Core::DepthPrePass::DepthPrePass(Device& device, WorkerThreadManager& workerThreadManager,
    Scene& scene, SwapChain& swapChain, VkFormat depthFormat,
    VkSampleCountFlagBits msaaSamples)
    : RendererPass(device, workerThreadManager)
    , _scene(scene)
    , _msaaSamples(msaaSamples)
{
    auto swapChainExtent = swapChain.GetSwapChainExtent();
    _extent = { swapChainExtent.width, swapChainExtent.height };

    auto& multiSampling = _pipelineState->GetMultisampleStateCreateInfo();
    multiSampling.rasterizationSamples = msaaSamples;

    // [0] Normal color attachment
    // Pass 1 finalLayout should be COLOR_ATTACHMENT_OPTIMAL to allow Pass 2 to start correctly
    _renderPass->CreateColorAttachment(
        VK_FORMAT_R8G8B8A8_UNORM, msaaSamples,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    // [1] Depth attachment
    _renderPass->CreateDepthAttachment(depthFormat, msaaSamples,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);

    _renderPass->CreateRenderPass();

    // Pass 2 RenderPass (LOAD instead of CLEAR for 2-pass occlusion culling)
    // initialLayout will be COLOR_ATTACHMENT_OPTIMAL (from Pass 1 finalLayout)
    // finalLayout is SHADER_READ_ONLY_OPTIMAL for later sampling
    _renderPassPass2 = new RenderPass(device);
    _renderPassPass2->CreateColorAttachment(
        VK_FORMAT_R8G8B8A8_UNORM, msaaSamples,
        VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    _renderPassPass2->CreateDepthAttachment(depthFormat, msaaSamples,
        VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);
    _renderPassPass2->CreateRenderPass();

    // Get the DepthNormal shader
    _depthNormalShader = _device.GetResourceCache().LoadShader("DepthNormal");

    // Create Pipeline for this pass
    _pipeline = new Pipeline(device, *_renderPass, _depthNormalShader.Get(), *_pipelineState);
}

Core::DepthPrePass::~DepthPrePass()
{
    delete(_pipeline);
    delete(_renderPassPass2);
}

void Core::DepthPrePass::EnsureRenderTargets(RenderFrame& renderFrame)
{
    auto& frameResources = renderFrame.GetResources();
    // [0] Normal RT
    RenderTargetDesc normalDesc{};
    normalDesc.extent  = _extent;
    normalDesc.format  = VK_FORMAT_R8G8B8A8_UNORM;
    normalDesc.usage   = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    normalDesc.samples = _msaaSamples;
    normalDesc.aspect  = VK_IMAGE_ASPECT_COLOR_BIT;
    auto normalTexture = frameResources.GetOrCreateRenderTarget(RT_MAIN_NORMAL, normalDesc);

    // [1] Depth RT
    RenderTargetDesc depthDesc{};
    depthDesc.extent  = _extent;
    depthDesc.format  = VK_FORMAT_UNDEFINED;
    depthDesc.usage   = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    depthDesc.samples = _msaaSamples;
    depthDesc.aspect  = VK_IMAGE_ASPECT_DEPTH_BIT;
    auto depthTexture = frameResources.GetOrCreateRenderTarget(RT_MAIN_DEPTH, depthDesc);

    // If MSAA is disabled, set current depth/normal directly (no resolve needed)
    if (_msaaSamples == VK_SAMPLE_COUNT_1_BIT)
    {
        frameResources.SetCurrentDepth(depthTexture);
        frameResources.SetCurrentNormal(normalTexture);
    }
}

void Core::DepthPrePass::Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer, uint32_t imageIndex)
{
	auto& frameResources = renderFrame.GetResources();
	auto* framebuffer = frameResources.GetOrCreateFramebuffer(
		"DepthPrePass",
		*_renderPass,
		{ RT_MAIN_NORMAL, RT_MAIN_DEPTH });

	if (!framebuffer)
		return;
	PerspectiveCamera* camera = _scene.GetMainCamera();

	commandBuffer.SetViewportAndScissor(framebuffer->GetExtent());

	auto& cameraBuffer = frameResources.GetOrCreateUniformBuffer<CameraBuffer>(UB_CAMERA);

	auto& depthNormalShader = _depthNormalShader.Get();
	auto builder = frameResources.CreateDescriptorSetBuilder(depthNormalShader, 0);
	builder.SetUniformBuffer(0, cameraBuffer);

	auto& executor = renderFrame.GetRenderExecutor();
	executor.OcclusionCullAndDraw(
		commandBuffer,
		depthNormalShader, *_pipeline,
		camera->Matrices,
		*_renderPass, *_renderPassPass2,
		*framebuffer,
		builder, nullptr,
		nullptr);
}
