#include "stdafx.h"
#include "RenderExecutor.h"
#include "RenderFrame.h"
#include "RendererBatch.h"
#include "Culler.h"
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

RenderExecutor::~RenderExecutor()
{
    if (_transformBatch.TransformBuffer)
    {
        delete _transformBatch.TransformBuffer;
        _transformBatch.TransformBuffer = nullptr;
    }
}

void RenderExecutor::InitializeBatches(Scene& scene, VkExtent2D extents)
{
    if (_batchesInitialized)
        return;

    auto meshes = scene.GetComponents<Mesh>();
    size_t meshCount = meshes.size();

    if (meshCount == 0)
    {
        _batchesInitialized = true;
        return;
    }

    VkDeviceSize bufferSize = sizeof(mat4) * meshCount;
    vector<mat4> transforms(meshCount);
    _transformBatch.EntityIds.resize(meshCount);

    for (size_t i = 0; i < meshCount; ++i)
    {
        auto& entity = meshes[i]->GetEntity();
        auto& transform = entity.GetTransform();
        transforms[i] = transform.GetMatrix();
        _transformBatch.EntityIds[i] = static_cast<uint>(entity.GetId());
    }

    _transformBatch.TransformBuffer = new Buffer(_device,
        bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        MemoryType::DEVICE_LOCAL);

    VkBufferJob<mat4> job(_device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        &_transformBatch.TransformBuffer, transforms, true);
    CommandBuffer::ImmediateSubmit(_device, job);

    _rendererBatch = make_unique<RendererBatch>(_device, scene, _transformBatch, extents);

    _batchesInitialized = true;
}

void RenderExecutor::ResetFrame()
{
    for (auto& [key, culler] : _cullers)
    {
        culler->MarkUsedThisFrame(false);
    }
}

Culler* RenderExecutor::GetOrCreateCuller(RendererBatch* batch, const CameraBuffer& camera, TransformBatch& transformBatch)
{
    CullerKey key{ &camera, batch };
    auto it = _cullers.find(key);
    if (it != _cullers.end())
        return it->second.get();

    auto culler = make_unique<Culler>(_device, transformBatch);
    auto* result = culler.get();
    _cullers[key] = std::move(culler);
    return result;
}

void RenderExecutor::OcclusionCullAndDraw(CommandBuffer& commandBuffer,
    Shader& shader, Pipeline& pipeline,
    CameraBuffer& camera,
    Core::RenderPass& pass1RenderPass, Core::RenderPass& pass2RenderPass,
    Framebuffer& framebuffer,
    DescriptorSetBuilder& builder,
    function<void(shared_ptr<Material>)> perDraw,
    function<void()> postDraw)
{
    auto* culler = GetOrCreateCuller(_rendererBatch.get(), camera, _transformBatch);

    if (!culler->IsPrepared())
    {
        culler->Prepare(_device, _rendererBatch->GetExtents(),
            _rendererBatch->GetObjectDataBuffer(), _rendererBatch->GetInstanceBuffer(),
            _rendererBatch->GetInstanceCount(), _rendererBatch->GetIndirectDrawBuffer());
    }

    bool cullerAlreadyUsed = culler->IsUsedThisFrame();

    if (!cullerAlreadyUsed)
    {
        auto prevDepth = _renderFrame.GetPreviousDepthBuffer();

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
    DrawIndirectInternal(commandBuffer, shader, pipeline, *culler->GetIndirectCommandBuffer(), builder, perDraw);
    commandBuffer.EndRenderPass();
    commandBuffer.EndDebugMarker();

    if (!cullerAlreadyUsed)
    {
        commandBuffer.BeginDebugMarker("Resolve Depth for Pass 2");
        auto msaaDepth = _renderFrame.GetRenderTarget("MainDepth");

        commandBuffer.TransitionImageLayout(*msaaDepth->GetImage().lock(),
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        auto curDepth = ResolvePass::ResolveDepth(_renderFrame, commandBuffer, msaaDepth);

        commandBuffer.TransitionImageLayout(*msaaDepth->GetImage().lock(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        commandBuffer.EndDebugMarker();

        commandBuffer.BeginDebugMarker("Pass 2 Culling");
        culler->DispatchPass2Culling(_renderFrame, commandBuffer, camera, curDepth);
        commandBuffer.EndDebugMarker();
    }

    const char* pass2Label = cullerAlreadyUsed ? "Pass 2 Render Newly Visible Objects (Reuse)" : "Pass 2 Render Newly Visible Objects";
    commandBuffer.BeginDebugMarker(pass2Label);
    auto pass2BeginInfo = pass2RenderPass.CreateRenderPassBeginInfo(framebuffer);
    commandBuffer.BeginRenderPass(pass2BeginInfo);
    DrawIndirectInternal(commandBuffer, shader, pipeline, *culler->GetPass2IndirectCommandBuffer(), builder, perDraw);

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

void RenderExecutor::FrustumCullAndDraw(
    CommandBuffer& commandBuffer,
    Core::RenderPass& renderPass,
    Framebuffer& framebuffer,
    Shader& shader,
    Pipeline& pipeline,
    DescriptorSetBuilder& builder,
    const CameraBuffer& camera,
    function<void(shared_ptr<Material>)> perDraw)
{
    if (_rendererBatch->GetDrawCommandCount() == 0)
        return;

    auto* culler = GetOrCreateCuller(_rendererBatch.get(), camera, _transformBatch);

    if (!culler->IsPrepared())
    {
        culler->Prepare(_device, _rendererBatch->GetExtents(),
            _rendererBatch->GetObjectDataBuffer(), _rendererBatch->GetInstanceBuffer(),
            _rendererBatch->GetInstanceCount(), _rendererBatch->GetIndirectDrawBuffer());
    }

    // Frustum culling dispatch
    auto cullingBuilder = _renderFrame.CreateDescriptorSetBuilder(culler->GetFrustumCullingShader());
    culler->DispatchFrustumOnlyCulling(_renderFrame, commandBuffer, cullingBuilder, camera);

    commandBuffer.BeginRenderPass(renderPass.CreateRenderPassBeginInfo(framebuffer));

    DrawIndirectInternal(commandBuffer, shader, pipeline, *culler->GetIndirectCommandBuffer(), builder, perDraw);

    commandBuffer.EndRenderPass();
}

void RenderExecutor::DrawIndirectInternal(CommandBuffer& commandBuffer,
    Shader& shader, Pipeline& pipeline,
    Core::Buffer& indirectCommandBuffer,
    DescriptorSetBuilder& builder, function<void(shared_ptr<Material>)> perDraw)
{
    if (_rendererBatch->GetDrawCommandCount() == 0)
        return;

    auto* meshBufferManager = _renderFrame.GetMeshBufferManager();

    auto vertexAttibuteNames = shader.GetVertexAttirbuteNames();
    commandBuffer.BindVertexBuffers(meshBufferManager->GetVertexBuffers(vertexAttibuteNames), 0);
    commandBuffer.BindIndexBuffer(meshBufferManager->GetIndexBuffer(), meshBufferManager->GetIndexType());

    commandBuffer.BindPipeline(&pipeline);

    builder.SetStorageBuffer(1, _transformBatch.TransformBuffer);
    builder.SetStorageBuffer(2, _rendererBatch->GetInstanceBuffer());
    builder.SetUniformBuffer(8,
        const_cast<GPUMaterialData*>(_renderFrame.GetMaterialManager()->GetMaterialData()));
    builder.SetStorageBuffer(9, _rendererBatch->GetMaterialIndexBuffer());

    auto& resources = builder.Build();

    vector<DescriptorSetResources*> resourcesList = { &resources };
    if (shader.UsesBindlessTextures())
    {
        auto* bindlessResources = _renderFrame.GetBindlessResources();
        if (bindlessResources)
            resourcesList.push_back(bindlessResources);
    }

    commandBuffer.BindDescriptorSets(_renderFrame, VK_PIPELINE_BIND_POINT_GRAPHICS, shader, resourcesList);

    auto material = _rendererBatch->GetFirstMaterial();
    perDraw(material);

    commandBuffer.DrawIndexedIndirect(
        indirectCommandBuffer,
        _rendererBatch->GetDrawCommandCount(),
        static_cast<uint32_t>(IndirectDrawBuffer::GetDrawCommandSize())
    );
}
