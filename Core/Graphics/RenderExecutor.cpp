#include "stdafx.h"
#include "RenderExecutor.h"
#include "RenderFrame.h"
#include "RendererBatch.h"
#include "OcclusionCuller.h"
#include "FrustumCuller.h"
#include "Vulkans/Shader.h"
#include "Vulkans/Pipeline.h"
#include "Vulkans/CommandBuffer.h"
#include "Vulkans/DescriptorSetBuilder.h"
#include "Vulkans/Texture.h"
#include "Foundation/Scene.h"
#include "Components/Mesh.h"
#include "Components/Transform.h"
#include "Foundation/Entity.h"
#include "Material.h"
#include "TransferJob.h"
#include "ResourceManager.h"
#include "FrameGraph/FrameGraphPass.h"

using namespace Core;

RenderExecutor::RenderExecutor(Device& device, RenderFrame& renderFrame)
    : _device(device)
    , _renderFrame(renderFrame)
{
    _depthResolveShader = _device.GetResourceManager().LoadShader("Shaders/depthResolve.comp.spv");
    _depthResolvePipeline = make_unique<Pipeline>(_device, _depthResolveShader.Get());
}

RenderExecutor::~RenderExecutor() = default;

RendererBatch* RenderExecutor::GetRendererBatch() const
{
    return &_renderFrame.GetRendererBatch();
}

MeshBufferManager& RenderExecutor::GetMeshBufferManager() const
{
    return _renderFrame.GetMeshBufferManager();
}

bool RenderExecutor::HasBindlessSupport() const
{
    return _renderFrame.HasBindlessSupport();
}

BindlessTextureManager* RenderExecutor::GetBindlessTextureManager() const
{
    return _renderFrame.GetBindlessTextureManager();
}

void RenderExecutor::ResetFrame()
{
    for (auto& [key, culler] : _cullers)
    {
        culler->MarkUsedThisFrame(false);
    }
}

Core::OcclusionCuller* RenderExecutor::PrepareOcclusionCuller(CameraBuffer& camera)
{
    auto* batch = GetRendererBatch();
    if (!batch || batch->GetDrawCommandCount() == 0)
        return nullptr;

    auto* culler = GetOrCreateCuller<OcclusionCuller>(*batch, camera);
    culler->SetCamera(camera);
    return culler;
}

void RenderExecutor::OcclusionCullAndDraw(CommandBuffer& commandBuffer,
    Shader& shader, Pipeline& pipeline,
    OcclusionCuller& occlusionCuller,
    FrameGraphPassContext& context,
    Texture& colorTarget, Texture& depthTarget,
    DescriptorSetBuilder& builder, function<void(Shader&)> perShaderHook,
    function<void()> postDraw)
{
    auto& frameResources = _renderFrame.GetResources();
    auto* culler = &occlusionCuller;
    CameraBuffer& camera = culler->GetCamera();

    bool cullerAlreadyUsed = culler->IsUsedThisFrame();

    if (!cullerAlreadyUsed)
    {
        auto prevDepth = frameResources.GetPreviousDepthBuffer();

        commandBuffer.BeginDebugMarker("Reset Draw Commands");
        culler->ResetDrawCommands(_renderFrame, commandBuffer);
        commandBuffer.EndDebugMarker();

        commandBuffer.BeginDebugMarker("Pass 1 Culling");
        culler->DispatchPass1Culling(_renderFrame, commandBuffer, camera, prevDepth);
        commandBuffer.EndDebugMarker();
    }

    const char* pass1Label = cullerAlreadyUsed ? "Pass 1 Render Visible Objects (Reuse)" : "Pass 1 Render Visible Objects";
    commandBuffer.BeginDebugMarker(pass1Label);
    context.BeginRendering(commandBuffer, 0);
    DrawIndirectInternal(commandBuffer, shader, pipeline, *culler->GetIndirectCommandBuffer(), builder, perShaderHook);
    context.EndRendering(commandBuffer);
    commandBuffer.EndDebugMarker();

    // The two legacy VkRenderPasses ordered phase 1 against phase 2 through
    // their external subpass dependencies; with dynamic rendering the
    // attachment barriers are explicit.
    commandBuffer.CreateBarrierBatch2()
        .Image(colorTarget,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT)
        .Image(depthTarget,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)
        .Submit();

    if (!cullerAlreadyUsed)
    {
        commandBuffer.BeginDebugMarker("Resolve Depth for Pass 2");
        auto msaaDepth = frameResources.GetRenderTarget("MainDepth");

        commandBuffer.CreateBarrierBatch()
            .Image(msaaDepth.Get(),
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            .Submit();

        auto curDepth = ResolveDepthForCulling(commandBuffer, msaaDepth);

        commandBuffer.CreateBarrierBatch()
            .Image(msaaDepth.Get(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
            .Submit();

        commandBuffer.EndDebugMarker();

        commandBuffer.BeginDebugMarker("Pass 2 Culling");
        culler->DispatchPass2Culling(_renderFrame, commandBuffer, camera, curDepth);
        commandBuffer.EndDebugMarker();
    }

    const char* pass2Label = cullerAlreadyUsed ? "Pass 2 Render Newly Visible Objects (Reuse)" : "Pass 2 Render Newly Visible Objects";
    commandBuffer.BeginDebugMarker(pass2Label);
    context.BeginRendering(commandBuffer, 1);
    DrawIndirectInternal(commandBuffer, shader, pipeline, *culler->GetPass2IndirectCommandBuffer(), builder, perShaderHook);

    if (postDraw)
    {
        commandBuffer.BeginDebugMarker("Post Draw");
        postDraw();
        commandBuffer.EndDebugMarker();
    }

    context.EndRendering(commandBuffer);
    commandBuffer.EndDebugMarker();

    if (!cullerAlreadyUsed)
    {
        culler->MarkUsedThisFrame(true);
    }
}

Core::FrustumCuller* RenderExecutor::PrepareFrustumCuller(CameraBuffer& camera)
{
    auto* batch = GetRendererBatch();
    if (!batch || batch->GetDrawCommandCount() == 0)
        return nullptr;

    auto* culler = GetOrCreateCuller<FrustumCuller>(*batch, camera);
    culler->SetCamera(camera);
    return culler;
}

void RenderExecutor::FrustumCullAndDraw(CommandBuffer& commandBuffer,
    FrustumCuller& culler,
    Shader& shader,
    Pipeline& pipeline,
    const VkRenderingInfo& renderingInfo,
    DescriptorSetBuilder& builder, function<void(Shader&)> perShaderHook)
{
    CameraBuffer& camera = culler.GetCamera();

    // Frustum culling dispatch (compute, outside the rendering scope)
    auto cullingBuilder = _renderFrame.GetResources().CreateDescriptorSetBuilder(culler.GetCullingShader());
    culler.Dispatch(_renderFrame, commandBuffer, cullingBuilder, camera);

    commandBuffer.BeginRendering(renderingInfo);

    DrawIndirectInternal(commandBuffer, shader, pipeline, *culler.GetIndirectCommandBuffer(), builder, perShaderHook);

    commandBuffer.EndRendering();
}

Handle<Texture> RenderExecutor::ResolveDepthForCulling(CommandBuffer& commandBuffer,
    Handle<Texture> msaaDepth)
{
    // Created by FGDepthPrePass's Setup; only looked up at record time.
    auto resolvedDepth = _renderFrame.GetResources().GetRenderTarget("ResolvedDepth");

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
    auto builder = _renderFrame.GetResources().CreateDescriptorSetBuilder(depthResolveShader, 0);
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

void RenderExecutor::DrawIndirectInternal(CommandBuffer& commandBuffer,
    Shader& shader, Pipeline& pipeline,
    Core::Buffer& indirectCommandBuffer,
    DescriptorSetBuilder& builder, function<void(Shader&)> perShaderHook)
{
    auto* batch = GetRendererBatch();
    if (!batch || batch->GetDrawCommandCount() == 0)
        return;

    auto& meshBufferManager = _renderFrame.GetMeshBufferManager();

    auto vertexAttibuteNames = shader.GetVertexAttirbuteNames();

    auto vertexBufferHandles = meshBufferManager.GetVertexBuffers(vertexAttibuteNames);
    vector<Buffer*> vertexBuffers;
    vertexBuffers.reserve(vertexBufferHandles.size());
    for (auto& handle : vertexBufferHandles)
        vertexBuffers.push_back(&handle.Get());

    commandBuffer.BindVertexBuffers(vertexBuffers, 0);
    commandBuffer.BindIndexBuffer(meshBufferManager.GetIndexBuffer().Get(), meshBufferManager.GetIndexType());

    commandBuffer.BindPipeline(&pipeline);

    builder.SetStorageBuffer(1, batch->GetTransformBatch().TransformBuffer.Get());
    builder.SetStorageBuffer(2, batch->GetInstanceBuffer());
    builder.SetUniformBuffer(8, _renderFrame.GetMaterialManager().GetMaterialBuffer());
    builder.SetStorageBuffer(9, batch->GetMaterialIndexBuffer());

    // Runs before Build() so the hook can contribute its own descriptor resources.
    if (perShaderHook)
        perShaderHook(shader);

    auto& resources = builder.Build();

    vector<DescriptorSetResources*> resourcesList = { &resources };
    if (shader.UsesBindlessTextures())
    {
        auto* bindlessResources = _renderFrame.GetBindlessResources();
        if (bindlessResources)
            resourcesList.push_back(bindlessResources);
    }

    commandBuffer.BindDescriptorSets(pipeline.GetPipelineBindPoint(), shader, resourcesList);

    commandBuffer.DrawIndexedIndirect(
        indirectCommandBuffer,
        batch->GetDrawCommandCount(),
        static_cast<uint32_t>(IndirectDrawBuffer::GetDrawCommandSize())
    );
}
