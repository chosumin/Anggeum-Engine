#include "stdafx.h"
#include "RenderExecutor.h"
#include "RenderFrame.h"
#include "RendererBatch.h"
#include "OcclusionCuller.h"
#include "FrustumCuller.h"
#include "Vulkans/Shader.h"
#include "Vulkans/Pipeline.h"
#include "Vulkans/RenderPass.h"
#include "Vulkans/CommandBuffer.h"
#include "Vulkans/DescriptorSetBuilder.h"
#include "Vulkans/Texture.h"
#include "Foundation/Scene.h"
#include "Components/Mesh.h"
#include "Components/Transform.h"
#include "Foundation/Entity.h"
#include "Material.h"
#include "TransferJob.h"
#include "RendererPasses/ResolvePass.h"

using namespace Core;

RenderExecutor::RenderExecutor(Device& device, RenderFrame& renderFrame)
    : _device(device)
    , _renderFrame(renderFrame)
{
}

RenderExecutor::~RenderExecutor() = default;

RendererBatch* RenderExecutor::GetRendererBatch() const
{
    return &_renderFrame.GetRendererBatch();
}

void RenderExecutor::ResetFrame()
{
    for (auto& [key, culler] : _cullers)
    {
        culler->MarkUsedThisFrame(false);
    }
}

void RenderExecutor::OcclusionCullAndDraw(CommandBuffer& commandBuffer,
    Shader& shader, Pipeline& pipeline,
    CameraBuffer& camera,
    Core::RenderPass& pass1RenderPass, Core::RenderPass& pass2RenderPass,
    Framebuffer& framebuffer,
    DescriptorSetBuilder& builder, function<void(Shader&)> perShaderHook,
    function<void()> postDraw)
{
    auto& frameResources = _renderFrame.GetResources();
    auto* batch = GetRendererBatch();
    // Nothing to cull: with no draw commands the batch has no instance/object
    // buffers for the Culler to read, so it must not be constructed either.
    if (!batch || batch->GetDrawCommandCount() == 0)
        return;

    auto* culler = GetOrCreateCuller<OcclusionCuller>(*batch, camera);

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
    auto pass1BeginInfo = pass1RenderPass.CreateRenderPassBeginInfo(framebuffer);
    commandBuffer.BeginRenderPass(pass1BeginInfo);
    DrawIndirectInternal(commandBuffer, shader, pipeline, *culler->GetIndirectCommandBuffer(), builder, perShaderHook);
    commandBuffer.EndRenderPass();
    commandBuffer.EndDebugMarker();

    if (!cullerAlreadyUsed)
    {
        commandBuffer.BeginDebugMarker("Resolve Depth for Pass 2");
        auto msaaDepth = frameResources.GetRenderTarget("MainDepth");

        commandBuffer.CreateBarrierBatch()
            .Image(msaaDepth.Get(),
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            .Submit();

        auto curDepth = ResolvePass::ResolveDepth(_renderFrame, commandBuffer, msaaDepth);

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
    auto pass2BeginInfo = pass2RenderPass.CreateRenderPassBeginInfo(framebuffer);
    commandBuffer.BeginRenderPass(pass2BeginInfo);
    DrawIndirectInternal(commandBuffer, shader, pipeline, *culler->GetPass2IndirectCommandBuffer(), builder, perShaderHook);

    if (postDraw)
    {
        commandBuffer.BeginDebugMarker("Post Draw");
        postDraw();
        commandBuffer.EndDebugMarker();
    }

    commandBuffer.EndRenderPass();
    commandBuffer.EndDebugMarker();

    if (!cullerAlreadyUsed)
    {
        culler->MarkUsedThisFrame(true);
    }
}

void RenderExecutor::FrustumCullAndDraw(CommandBuffer& commandBuffer,
    Core::RenderPass& renderPass,
    Framebuffer& framebuffer,
    Shader& shader,
    Pipeline& pipeline,
    DescriptorSetBuilder& builder,
    const CameraBuffer& camera, function<void(Shader&)> perShaderHook)
{
    auto* batch = GetRendererBatch();
    if (!batch || batch->GetDrawCommandCount() == 0)
        return;

    auto* culler = GetOrCreateCuller<FrustumCuller>(*batch, camera);

    // Frustum culling dispatch
    auto cullingBuilder = _renderFrame.GetResources().CreateDescriptorSetBuilder(culler->GetCullingShader());
    culler->Dispatch(_renderFrame, commandBuffer, cullingBuilder, camera);

    commandBuffer.BeginRenderPass(renderPass.CreateRenderPassBeginInfo(framebuffer));

    DrawIndirectInternal(commandBuffer, shader, pipeline, *culler->GetIndirectCommandBuffer(), builder, perShaderHook);

    commandBuffer.EndRenderPass();
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
