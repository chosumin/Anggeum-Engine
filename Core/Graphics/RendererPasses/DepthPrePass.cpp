#include "stdafx.h"
#include "DepthPrePass.h"
#include "Foundation/Scene.h"
#include "Components/PerspectiveCamera.h"
#include "Components/Mesh.h"
#include "Graphics/Vulkans/SwapChain.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Material.h"
#include "Graphics/ResourceCache.h"

using namespace Core;

Core::DepthPrePass::DepthPrePass(Device& device, WorkerThreadManager& workerThreadManager,
    Scene& scene, SwapChain& swapChain, VkFormat depthFormat,
    VkSampleCountFlagBits msaaSamples, TransformBatch& transformBatch)
    : RendererPass(device, workerThreadManager)
    , _scene(scene)
    , _msaaSamples(msaaSamples)
{
    auto swapChainExtent = swapChain.GetSwapChainExtent();
    _extent = { swapChainExtent.width, swapChainExtent.height };

    auto& multiSampling = _pipelineState->GetMultisampleStateCreateInfo();
    multiSampling.rasterizationSamples = msaaSamples;

	_renderPass->CreateDepthAttachment(depthFormat, msaaSamples,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
    _renderPass->CreateRenderPass();

    _depthMaterial = _device.GetResourceCache().RequestMaterial("depth", "Depth");

    _rendererBatches = make_unique<RendererBatches>(device, transformBatch);

    auto meshes = _scene.GetComponents<Core::Mesh>();
    _rendererBatches->PrepareSingleBatch(_device,
        _depthMaterial,
        *_renderPass, *_pipelineState, meshes);

    if (_device.IsGpuDrivenRenderingEnabled())
        _rendererBatches->PrepareGPUDrivenRendering(_device, false, swapChainExtent);
}

Core::DepthPrePass::~DepthPrePass()
{
}

void Core::DepthPrePass::EnsureRenderTargets(RenderFrame& renderFrame)
{
    RenderTargetDesc depthDesc{};
    depthDesc.extent = _extent;
    depthDesc.format = VK_FORMAT_UNDEFINED;
    depthDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    depthDesc.samples = _msaaSamples;
    depthDesc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    renderFrame.GetOrCreateRenderTarget(RT_MAIN_DEPTH, depthDesc);
}

void Core::DepthPrePass::Prepare()
{
}

void Core::DepthPrePass::Draw(RenderFrame& renderFrame, uint32_t imageIndex)
{
    EnsureRenderTargets(renderFrame);

    auto* framebuffer = renderFrame.GetOrCreateFramebuffer(
        "DepthPrePass",
        *_renderPass,
        { RT_MAIN_DEPTH });

    if (!framebuffer)
        return;

    auto& commandBuffer = renderFrame.GetCommandBuffer();
    PerspectiveCamera* camera = _scene.GetMainCamera();

    commandBuffer.SetViewportAndScissor(framebuffer->GetExtent());

    auto renderPassBeginInfo = _renderPass->CreateRenderPassBeginInfo(*framebuffer);
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
