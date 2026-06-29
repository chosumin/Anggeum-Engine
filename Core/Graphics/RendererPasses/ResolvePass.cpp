#include "stdafx.h"
#include "ResolvePass.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Graphics/ResourceCache.h"
#include "DepthPrePass.h"

using namespace Core;

ResolvePass::ResolvePass(Device& device, WorkerThreadManager& workerThreadManager,
    VkExtent2D screenExtent, VkSampleCountFlagBits msaaSamples)
    : RendererPass(device, workerThreadManager)
    , _screenExtent(screenExtent)
    , _msaaSamples(msaaSamples)
{
    if (_msaaSamples != VK_SAMPLE_COUNT_1_BIT)
    {
        _depthResolveShader = _device.GetResourceCache().RequestShader("Shaders/depthResolve.comp.spv");
        _depthResolvePipeline = make_unique<Pipeline>(_device, *_depthResolveShader);

        _normalResolveShader = _device.GetResourceCache().RequestShader("Shaders/normalResolve.comp.spv");
        _normalResolvePipeline = make_unique<Pipeline>(_device, *_normalResolveShader);
    }
}

ResolvePass::~ResolvePass()
{
}

void ResolvePass::EnsureRenderTargets(RenderFrame& renderFrame)
{
    if (_msaaSamples == VK_SAMPLE_COUNT_1_BIT)
        return;

    RenderTargetDesc resolvedDepthDesc{};
    resolvedDepthDesc.extent  = _screenExtent;
    resolvedDepthDesc.format  = VK_FORMAT_R32_SFLOAT;
    resolvedDepthDesc.usage   = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    resolvedDepthDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    resolvedDepthDesc.aspect  = VK_IMAGE_ASPECT_COLOR_BIT;
    _resolvedDepthTexture = renderFrame.GetOrCreateRenderTarget(RT_RESOLVED_DEPTH, resolvedDepthDesc);

    RenderTargetDesc resolvedNormalDesc{};
    resolvedNormalDesc.extent  = _screenExtent;
    resolvedNormalDesc.format  = VK_FORMAT_R8G8B8A8_UNORM;
    resolvedNormalDesc.usage   = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    resolvedNormalDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    resolvedNormalDesc.aspect  = VK_IMAGE_ASPECT_COLOR_BIT;
    _resolvedNormalTexture = renderFrame.GetOrCreateRenderTarget(RT_RESOLVED_NORMAL, resolvedNormalDesc);
}

void ResolvePass::Draw(RenderFrame& renderFrame, uint32_t imageIndex)
{
    if (_msaaSamples == VK_SAMPLE_COUNT_1_BIT)
        return;

    auto& commandBuffer = renderFrame.GetCommandBuffer();

    auto depthTexture = renderFrame.GetRenderTarget(DepthPrePass::RT_MAIN_DEPTH);
    auto normalTexture = renderFrame.GetRenderTarget(DepthPrePass::RT_MAIN_NORMAL);
    if (!depthTexture || !normalTexture)
        return;

    // Transition depth to shader read
    commandBuffer.TransitionImageLayout(*depthTexture->GetImage().lock(),
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Resolve depth
    commandBuffer.BeginDebugMarker("Resolve MSAA Depth");
    ResolveDepth(renderFrame, commandBuffer, depthTexture);
    commandBuffer.EndDebugMarker();

    // Resolve normal
    commandBuffer.BeginDebugMarker("Resolve MSAA Normal");
    ResolveNormal(renderFrame, commandBuffer, normalTexture);
    commandBuffer.EndDebugMarker();
}

void ResolvePass::ResolveDepth(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
    shared_ptr<Texture> msaaDepth)
{
    auto& resolvedImage = *_resolvedDepthTexture->GetImage().lock();
    commandBuffer.TransitionImageLayout(resolvedImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    struct DepthResolvePushConstants {
        int32_t outputWidth;
        int32_t outputHeight;
        int32_t sampleCount;
        int32_t padding;
    } resolvePc = {
        static_cast<int32_t>(_screenExtent.width),
        static_cast<int32_t>(_screenExtent.height),
        static_cast<int32_t>(_msaaSamples),
        0
    };

    auto builder = renderFrame.CreateDescriptorSetBuilder(*_depthResolveShader, 0);
    builder.SetTextureBuffer(0, msaaDepth);
    builder.SetTextureBuffer(1, _resolvedDepthTexture, 0, VK_IMAGE_LAYOUT_GENERAL);
    auto& resources = builder.Build();

    commandBuffer.BindPipeline(_depthResolvePipeline.get());
    commandBuffer.BindDescriptorSet(renderFrame,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        *_depthResolveShader, 0, resources);
    commandBuffer.PushConstants(*_depthResolveShader, 0, &resolvePc);

    uint32_t groupX = (_screenExtent.width + 7) / 8;
    uint32_t groupY = (_screenExtent.height + 7) / 8;
    commandBuffer.Dispatch(groupX, groupY, 1);

    commandBuffer.TransitionImageLayout(resolvedImage,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void ResolvePass::ResolveNormal(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
    shared_ptr<Texture> msaaNormal)
{
    auto& resolvedImage = *_resolvedNormalTexture->GetImage().lock();
    commandBuffer.TransitionImageLayout(resolvedImage,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

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

    auto builder = renderFrame.CreateDescriptorSetBuilder(*_normalResolveShader, 0);
    builder.SetTextureBuffer(0, msaaNormal);
    builder.SetTextureBuffer(1, _resolvedNormalTexture, 0, VK_IMAGE_LAYOUT_GENERAL);
    auto& resources = builder.Build();

    commandBuffer.BindPipeline(_normalResolvePipeline.get());
    commandBuffer.BindDescriptorSet(renderFrame,
        VK_PIPELINE_BIND_POINT_COMPUTE, *_normalResolveShader, 0, resources);
    commandBuffer.PushConstants(*_normalResolveShader, 0, &pc);

    commandBuffer.Dispatch(
        (_screenExtent.width + 7) / 8,
        (_screenExtent.height + 7) / 8, 1);

    commandBuffer.TransitionImageLayout(resolvedImage,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
