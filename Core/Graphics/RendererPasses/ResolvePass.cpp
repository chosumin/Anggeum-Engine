#include "stdafx.h"
#include "ResolvePass.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Graphics/ResourceManager.h"
#include "DepthPrePass.h"

using namespace Core;

// Static member definitions
VkExtent2D ResolvePass::_screenExtent = {};
Handle<Shader> ResolvePass::_depthResolveShader;
unique_ptr<Pipeline> ResolvePass::_depthResolvePipeline = nullptr;

ResolvePass::ResolvePass(Device& device, WorkerThreadManager& workerThreadManager,
    VkExtent2D screenExtent, VkSampleCountFlagBits msaaSamples)
    : RendererPass(device, workerThreadManager)
    , _msaaSamples(msaaSamples)
{
    if (_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
    {
        _depthResolveShader = _device.GetResourceManager().LoadShader("Shaders/depthResolve.comp.spv");
        _depthResolvePipeline = make_unique<Pipeline>(_device, _depthResolveShader.Get());

        _normalResolveShader = _device.GetResourceManager().LoadShader("Shaders/normalResolve.comp.spv");
        _normalResolvePipeline = make_unique<Pipeline>(_device, _normalResolveShader.Get());

        _screenExtent = screenExtent;
    }
}

ResolvePass::~ResolvePass()
{
    _depthResolvePipeline.reset();
    _depthResolveShader = Handle<Shader>{};
}

void ResolvePass::EnsureRenderTargets(RenderFrame& renderFrame)
{
    auto& frameResources = renderFrame.GetResources();
    if (_msaaSamples == VK_SAMPLE_COUNT_1_BIT)
        return;

	_resolvedDepthTexture = GetResolvedDepthTarget(renderFrame);

    RenderTargetDesc resolvedNormalDesc{};
    resolvedNormalDesc.extent  = _screenExtent;
    resolvedNormalDesc.format  = VK_FORMAT_R8G8B8A8_UNORM;
    resolvedNormalDesc.usage   = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    resolvedNormalDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    resolvedNormalDesc.aspect  = VK_IMAGE_ASPECT_COLOR_BIT;
    _resolvedNormalTexture = frameResources.GetOrCreateRenderTarget(RT_RESOLVED_NORMAL, resolvedNormalDesc);

    frameResources.SetCurrentDepth(_resolvedDepthTexture);
    frameResources.SetCurrentNormal(_resolvedNormalTexture);
}

void ResolvePass::Draw(RenderFrame& renderFrame, CommandBuffer& commandBuffer, uint32_t imageIndex)
{
    auto& frameResources = renderFrame.GetResources();
    if (_msaaSamples == VK_SAMPLE_COUNT_1_BIT)
        return;

    auto depthTexture = frameResources.GetRenderTarget(DepthPrePass::RT_MAIN_DEPTH);
    auto normalTexture = frameResources.GetRenderTarget(DepthPrePass::RT_MAIN_NORMAL);

    commandBuffer.CreateBarrierBatch()
        .Image(depthTexture.Get(),
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .Submit();

    // Resolve depth
    commandBuffer.BeginDebugMarker("Resolve MSAA Depth");
    ResolveDepth(renderFrame, commandBuffer, depthTexture, QueueType::Compute);
    commandBuffer.EndDebugMarker();

    // Resolve normal
    commandBuffer.BeginDebugMarker("Resolve MSAA Normal");
    ResolveNormal(renderFrame, commandBuffer, normalTexture);
    commandBuffer.EndDebugMarker();

    // Signal graphics timeline so compute passes (LightCulling) can consume the resolved depth
    renderFrame.GetCurrentSubmitInfo().AddSignalSemaphore(GetQueueType());
}

Handle<Texture> ResolvePass::ResolveDepth(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
    Handle<Texture> msaaDepth, QueueType destQueue)
{
    auto resolvedDepth = GetResolvedDepthTarget(renderFrame);

    commandBuffer.CreateBarrierBatch()
        .Image(resolvedDepth.Get(),
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL)
        .Submit();

	auto extent = resolvedDepth.Get().GetExtent();

    struct DepthResolvePushConstants {
        int32_t outputWidth;
        int32_t outputHeight;
        int32_t sampleCount;
        int32_t padding;
    } resolvePc = {
        static_cast<int32_t>(extent.width),
        static_cast<int32_t>(extent.height),
        static_cast<int32_t>(msaaDepth.Get().GetSampleCount()),
        0
    };

    auto& depthResolveShader = _depthResolveShader.Get();
    auto builder = renderFrame.GetResources().CreateDescriptorSetBuilder(depthResolveShader, 0);
    builder.SetTextureBuffer(0, msaaDepth);
    builder.SetTextureBuffer(1, resolvedDepth, 0, VK_IMAGE_LAYOUT_GENERAL);
    auto& resources = builder.Build();

    commandBuffer.BindPipeline(_depthResolvePipeline.get());
    commandBuffer.BindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE,
        depthResolveShader,
        resources);
    commandBuffer.PushConstants(depthResolveShader, 0, resolvePc);

    uint32_t groupX = (extent.width + 7) / 8;
    uint32_t groupY = (extent.height + 7) / 8;
    commandBuffer.Dispatch(groupX, groupY, 1);

    commandBuffer.CreateBarrierBatch()
        .Image(resolvedDepth.Get(),
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .Submit();

    return resolvedDepth;
}

void ResolvePass::ResolveNormal(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
    Handle<Texture> msaaNormal)
{
    commandBuffer.CreateBarrierBatch()
        .Image(_resolvedNormalTexture.Get(),
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL)
        .Submit();

    struct PushConstants {
        int32_t outputWidth;
        int32_t outputHeight;
        int32_t sampleCount;
        int32_t padding;
    } pc = {
        static_cast<int32_t>(_screenExtent.width),
        static_cast<int32_t>(_screenExtent.height),
        static_cast<int32_t>(_msaaSamples),
        0
    };

    auto& normalResolveShader = _normalResolveShader.Get();
    auto builder = renderFrame.GetResources().CreateDescriptorSetBuilder(normalResolveShader, 0);
    builder.SetTextureBuffer(0, msaaNormal);
    builder.SetTextureBuffer(1, _resolvedNormalTexture, 0, VK_IMAGE_LAYOUT_GENERAL);
    auto& resources = builder.Build();

    commandBuffer.BindPipeline(_normalResolvePipeline.get());
    commandBuffer.BindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE,
        normalResolveShader, resources);
    commandBuffer.PushConstants(normalResolveShader, 0, pc);

    commandBuffer.Dispatch(
        (_screenExtent.width + 7) / 8,
        (_screenExtent.height + 7) / 8, 1);

    commandBuffer.CreateBarrierBatch()
        .Image(_resolvedNormalTexture.Get(),
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .Submit();
}

Handle<Texture> Core::ResolvePass::GetResolvedDepthTarget(RenderFrame& renderFrame)
{
    RenderTargetDesc resolvedDepthDesc{};
    resolvedDepthDesc.extent = _screenExtent;
    resolvedDepthDesc.format = VK_FORMAT_R32_SFLOAT;
    resolvedDepthDesc.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    resolvedDepthDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    resolvedDepthDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    
    auto resolvedDepthTexture = renderFrame.GetResources().GetOrCreateRenderTarget(RT_RESOLVED_DEPTH, resolvedDepthDesc);

    return resolvedDepthTexture;
}
