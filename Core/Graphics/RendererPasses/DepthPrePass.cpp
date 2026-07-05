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

    // [0] Normal color attachment
    _renderPass->CreateColorAttachment(
        VK_FORMAT_R8G8B8A8_UNORM, msaaSamples,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // [1] Depth attachment
    _renderPass->CreateDepthAttachment(depthFormat, msaaSamples,
        VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);

    _renderPass->CreateRenderPass();

    _rendererBatches = make_unique<RendererBatches>(device, transformBatch);

    auto meshes = _scene.GetComponents<Core::Mesh>();
    _rendererBatches->Prepare(_device,
        "DepthNormal",
        *_renderPass, *_pipelineState, meshes, _overrideMaterials);

    _rendererBatches->PrepareGPUDrivenRendering(_device, true, swapChainExtent);
}

Core::DepthPrePass::~DepthPrePass()
{
}

void Core::DepthPrePass::EnsureRenderTargets(RenderFrame& renderFrame)
{
    // [0] Normal RT
    RenderTargetDesc normalDesc{};
    normalDesc.extent  = _extent;
    normalDesc.format  = VK_FORMAT_R8G8B8A8_UNORM;
    normalDesc.usage   = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    normalDesc.samples = _msaaSamples;
    normalDesc.aspect  = VK_IMAGE_ASPECT_COLOR_BIT;
    auto normalTexture = renderFrame.GetOrCreateRenderTarget(RT_MAIN_NORMAL, normalDesc);

    // [1] Depth RT
    RenderTargetDesc depthDesc{};
    depthDesc.extent  = _extent;
    depthDesc.format  = VK_FORMAT_UNDEFINED;
    depthDesc.usage   = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    depthDesc.samples = _msaaSamples;
    depthDesc.aspect  = VK_IMAGE_ASPECT_DEPTH_BIT;
    auto depthTexture = renderFrame.GetOrCreateRenderTarget(RT_MAIN_DEPTH, depthDesc);

    // If MSAA is disabled, set current depth/normal directly (no resolve needed)
    if (_msaaSamples == VK_SAMPLE_COUNT_1_BIT)
    {
        renderFrame.SetCurrentDepth(depthTexture);
        renderFrame.SetCurrentNormal(normalTexture);
    }
}

void Core::DepthPrePass::Draw(RenderFrame& renderFrame, uint32_t imageIndex)
{
    auto* framebuffer = renderFrame.GetOrCreateFramebuffer(
        "DepthPrePass",
        *_renderPass,
        { RT_MAIN_NORMAL, RT_MAIN_DEPTH });

    if (!framebuffer)
        return;

    auto& commandBuffer = renderFrame.GetCommandBuffer();
    PerspectiveCamera* camera = _scene.GetMainCamera();

    commandBuffer.BeginDebugMarker("Frustum Culling");
    _rendererBatches->DispatchFrustumOnlyCulling(
        renderFrame, commandBuffer, camera->Matrices);
    commandBuffer.EndDebugMarker();

    commandBuffer.SetViewportAndScissor(framebuffer->GetExtent());

    auto renderPassBeginInfo = _renderPass->CreateRenderPassBeginInfo(*framebuffer);
    commandBuffer.BeginRenderPass(renderPassBeginInfo);

    _rendererBatches->DrawIndirect(renderFrame, commandBuffer,
    [&](shared_ptr<Shader> shader)
    {
        renderFrame.SetShaderUniformBuffer(*shader, 0, &camera->Matrices);
    },
    [&](shared_ptr<Material> sharedMaterial)
    {
    });

    commandBuffer.EndRenderPass();
}
