#include "stdafx.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/Culler.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Material.h"
#include "Foundation/Entity.h"
#include "Foundation/Scene.h"
#include "Components/Mesh.h"
#include "Components/Transform.h"
#include "Graphics/SubMesh.h"
#include "Graphics/Vulkans/PipelineState.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/RenderPass.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/ResourceCache.h"
#include "Vulkans/Texture.h"
#include "Vulkans/DescriptorSetBuilder.h"
#include "TransferJob.h"
#include "RendererPasses/ResolvePass.h"

using namespace Core;

Core::RendererBatch::RendererBatch(Device& device, Scene& scene, TransformBatch& transformBatch, VkExtent2D extents)
    : _device(device)
    , _transformBatch(&transformBatch)
{
    auto meshes = scene.GetComponents<Mesh>();

    for (auto* mesh : meshes)
    {
        uint entityId = mesh->GetEntity().GetId();
        auto& materials = mesh->GetMaterials();
        auto& subMeshes = mesh->GetSubMeshes();

        for (size_t i = 0; i < materials.size(); ++i)
        {
            if (i >= subMeshes.size())
                break;

            auto shaderPtr = materials[i]->GetShaderPtr().lock();
            if (!shaderPtr)
                continue;

            // Skip non-geometry passes (Skybox, etc.)
            const string& pass = shaderPtr->GetPass();
            if (pass != "Geometry")
                continue;

            AddMesh(entityId, materials[i], subMeshes[i]);
        }
    }

    Finalize();
    PrepareGPUDrivenRendering(extents);
}

Core::RendererBatch::~RendererBatch()
{
    if (_materialIndexBuffer != nullptr) delete(_materialIndexBuffer);
    if (_indirectCommandBuffer != nullptr) delete(_indirectCommandBuffer);
    if (_instanceBuffer != nullptr) delete(_instanceBuffer);
    if (_objectDataBuffer != nullptr) delete(_objectDataBuffer);
}

void Core::RendererBatch::AddMesh(uint entityId, weak_ptr<Material> material, weak_ptr<SubMesh> subMesh)
{
    auto materialPtr = material.lock();
    if (!materialPtr)
        return;

    auto materialName = materialPtr->GetName();

    auto matPtr = _materialBatches.find(materialName);
    if (matPtr == _materialBatches.end())
    {
        MaterialBatch materialBatch;
        materialBatch.Material = material;
        _materialBatches.insert(make_pair(materialName, materialBatch));
    }

    auto& subMeshBatches = _materialBatches[materialName].SubMeshBatches;

    auto subMeshPtr = subMesh.lock();
    if (!subMeshPtr)
        return;

    string subMeshName = subMeshPtr->GetName();
    auto& subMeshBatch = subMeshBatches[subMeshName];

    if (subMeshBatch.Transforms.empty())
    {
        subMeshBatch.SubMesh = subMesh;
        subMeshBatch.FirstInstance = _instanceCount;
    }

    subMeshBatch.Transforms.push_back(entityId);
    _instanceCount++;
}

void Core::RendererBatch::Finalize()
{
    CreateInstanceBuffer(_device);
}

void Core::RendererBatch::PrepareGPUDrivenRendering(VkExtent2D extents)
{
    _indirectDrawBuffer.Clear();
    uint32_t globalFirstInstance = 0;
    uint32_t drawCommandIndex = 0;

    vector<GPUObjectData> objectData;
    objectData.reserve(_instanceCount);

    for (auto& [materialName, materialBatch] : _materialBatches)
    {
        auto material = materialBatch.Material.lock();
        if (!material || !material->HasMaterialIndex())
            continue;

        uint32_t materialIndex = material->GetMaterialIndex();

        for (auto& [subMeshName, subMeshBatch] : materialBatch.SubMeshBatches)
        {
            auto subMesh = subMeshBatch.SubMesh.lock();
            if (!subMesh || !subMesh->HasAllocation())
                continue;

            const auto& allocation = subMesh->GetAllocation();
            uint32_t instanceCount = static_cast<uint32_t>(subMeshBatch.Transforms.size());

            _indirectDrawBuffer.AddDrawCommand(
                allocation,
                materialIndex,
                0,
                globalFirstInstance
            );

            for (size_t i = 0; i < subMeshBatch.Transforms.size(); ++i)
            {
                GPUObjectData data{};
                data.boundingSphere = glm::vec4(
                    allocation.boundingSphereCenter,
                    allocation.boundingSphereRadius);
                data.transformIndex = subMeshBatch.Transforms[i];
                data.drawCommandIndex = drawCommandIndex;

                objectData.push_back(data);
            }

            globalFirstInstance += instanceCount;
            drawCommandIndex++;
        }
    }

    vector<Job*> jobs;

    Core::VkBufferJob<DrawIndexedIndirectCommand> job(_device,
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        &_indirectCommandBuffer,
        _indirectDrawBuffer.GetDrawCommands(), 0);
    jobs.push_back(&job);

    Core::VkBufferJob<uint32_t> job2(_device,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        &_materialIndexBuffer,
        _indirectDrawBuffer.GetMaterialIndices(), 0);
    jobs.push_back(&job2);

    Core::VkBufferJob<GPUObjectData> job3(_device,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        &_objectDataBuffer,
        objectData, 0);
    jobs.push_back(&job3);

    Core::CommandBuffer::ImmediateSubmit(_device, jobs);

    _extents = extents;
}

void Core::RendererBatch::GpuDrivenDraw(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
    Shader& shader, Pipeline& pipeline,
    CameraBuffer& camera,
    Core::RenderPass& pass1RenderPass, Core::RenderPass& pass2RenderPass,
    Framebuffer& framebuffer,
    function<void(Shader&)> perShader, function<void(shared_ptr<Material>)> perDraw,
    function<void()> postDraw)
{
    auto* culler = renderFrame.GetOrCreateCuller(this, camera, _device, *_transformBatch);

    if (!culler->IsPrepared())
    {
        culler->Prepare(_device, _extents,
            _objectDataBuffer, _instanceBuffer, _indirectCommandBuffer,
            _instanceCount, _indirectDrawBuffer);
    }

    bool cullerAlreadyUsed = culler->IsUsedThisFrame();

    if (!cullerAlreadyUsed)
    {
        auto prevDepth = renderFrame.GetPreviousDepthBuffer();
        auto curDepth = renderFrame.GetRenderTarget(ResolvePass::RT_RESOLVED_DEPTH);
        if (curDepth == nullptr)
            curDepth = renderFrame.GetRenderTarget("MainDepth");

        commandBuffer.BeginDebugMarker("Reset Draw Commands");
        culler->ResetDrawCommands(renderFrame, commandBuffer);
        commandBuffer.EndDebugMarker();

        commandBuffer.BeginDebugMarker("Pass 1 Culling");
        culler->DispatchPass1Culling(renderFrame, commandBuffer, camera, prevDepth);
        commandBuffer.EndDebugMarker();

        commandBuffer.BeginDebugMarker("Pass 1 Render Visible Objects");
        auto pass1BeginInfo = pass1RenderPass.CreateRenderPassBeginInfo(framebuffer);
        commandBuffer.BeginRenderPass(pass1BeginInfo);
        DrawIndirectInternal(renderFrame, commandBuffer, shader, pipeline, *_indirectCommandBuffer, perShader, perDraw);
        commandBuffer.EndRenderPass();
        commandBuffer.EndDebugMarker();

        commandBuffer.BeginDebugMarker("Pass 2 Culling");
        culler->DispatchPass2Culling(renderFrame, commandBuffer, camera, curDepth);
        commandBuffer.EndDebugMarker();

        commandBuffer.BeginDebugMarker("Pass 2 Render Newly Visible Objects");
        auto pass2BeginInfo = pass2RenderPass.CreateRenderPassBeginInfo(framebuffer);
        commandBuffer.BeginRenderPass(pass2BeginInfo);
        DrawIndirectInternal(renderFrame, commandBuffer, shader, pipeline, *culler->GetPass2IndirectCommandBuffer(), perShader, perDraw);

        if (postDraw)
        {
            commandBuffer.BeginDebugMarker("Post Draw");
            postDraw();
            commandBuffer.EndDebugMarker();
        }

        commandBuffer.EndRenderPass();
        commandBuffer.EndDebugMarker();

        culler->MarkUsedThisFrame(true);
    }
    else
    {
        commandBuffer.BeginDebugMarker("Pass 1 Render Visible Objects (Reuse)");
        auto pass1BeginInfo = pass1RenderPass.CreateRenderPassBeginInfo(framebuffer);
        commandBuffer.BeginRenderPass(pass1BeginInfo);
        DrawIndirectInternal(renderFrame, commandBuffer, shader, pipeline, *_indirectCommandBuffer, perShader, perDraw);
        commandBuffer.EndRenderPass();
        commandBuffer.EndDebugMarker();

        commandBuffer.BeginDebugMarker("Pass 2 Render Newly Visible Objects (Reuse)");
        auto pass2BeginInfo = pass2RenderPass.CreateRenderPassBeginInfo(framebuffer);
        commandBuffer.BeginRenderPass(pass2BeginInfo);
        DrawIndirectInternal(renderFrame, commandBuffer, shader, pipeline, *culler->GetPass2IndirectCommandBuffer(), perShader, perDraw);

        if (postDraw)
        {
            commandBuffer.BeginDebugMarker("Post Draw");
            postDraw();
            commandBuffer.EndDebugMarker();
        }

        commandBuffer.EndRenderPass();
        commandBuffer.EndDebugMarker();
    }
}

void Core::RendererBatch::DrawIndirect(
    RenderFrame& renderFrame,
    CommandBuffer& commandBuffer,
    Shader& shader, Pipeline& pipeline,
    function<void(Shader&)> perShader,
    function<void(shared_ptr<Material>)> perDraw)
{
    DrawIndirectInternal(renderFrame, commandBuffer, shader, pipeline, *_indirectCommandBuffer, perShader, perDraw);
}

void Core::RendererBatch::DrawIndirect(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
    Shader& shader, DescriptorSetBuilder& builder, function<void(shared_ptr<Material>)> perDraw)
{
    if (_indirectDrawBuffer.GetDrawCount() == 0)
        return;

    auto* meshBufferManager = renderFrame.GetMeshBufferManager();

    auto vertexAttibuteNames = shader.GetVertexAttirbuteNames();
    commandBuffer.BindVertexBuffers(meshBufferManager->GetVertexBuffers(vertexAttibuteNames), 0);
    commandBuffer.BindIndexBuffer(meshBufferManager->GetIndexBuffer(), meshBufferManager->GetIndexType());

	builder.SetStorageBuffer(1, _transformBatch->TransformBuffer);
	builder.SetStorageBuffer(2, _instanceBuffer);

	builder.SetUniformBuffer(8,
		const_cast<GPUMaterialData*>(renderFrame.GetMaterialManager()->GetMaterialData()));
	builder.SetStorageBuffer(9, _materialIndexBuffer);

    auto& resources = builder.Build();

    commandBuffer.BindDescriptorSet(
        renderFrame, VK_PIPELINE_BIND_POINT_GRAPHICS,
        shader, builder.GetSetIndex(), resources);

    auto material = _materialBatches.begin()->second.Material.lock();
    perDraw(material);

    commandBuffer.DrawIndexedIndirect(
        *_indirectCommandBuffer,
        _indirectDrawBuffer.GetDrawCount(),
        static_cast<uint32_t>(IndirectDrawBuffer::GetDrawCommandSize())
    );
}

void Core::RendererBatch::CreateInstanceBuffer(Device& device)
{
    if (_instanceCount == 0)
        return;

    vector<uint> instanceData(_instanceCount);

    uint i = 0;
    for (auto& [materialName, materialBatch] : _materialBatches)
    {
        for (auto& [subMeshName, subMeshBatch] : materialBatch.SubMeshBatches)
        {
            for (auto& transform : subMeshBatch.Transforms)
            {
                instanceData[i++] = transform;
            }
        }
    }

    _instanceBuffer = new Core::Buffer(device, _instanceCount * sizeof(uint),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, MemoryType::DEVICE_LOCAL);

    Core::VkBufferJob<uint> job(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &_instanceBuffer, instanceData, true);
    Core::CommandBuffer::ImmediateSubmit(device, job);
}

void Core::RendererBatch::DrawIndirectInternal(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
    Shader& shader, Pipeline& pipeline,
    Core::Buffer& indirectCommandBuffer,
    function<void(Shader&)> perShader, function<void(shared_ptr<Material>)> perDraw)
{
    if (_indirectDrawBuffer.GetDrawCount() == 0)
        return;

    auto* meshBufferManager = renderFrame.GetMeshBufferManager();

    auto vertexAttibuteNames = shader.GetVertexAttirbuteNames();
    commandBuffer.BindVertexBuffers(meshBufferManager->GetVertexBuffers(vertexAttibuteNames), 0);
    commandBuffer.BindIndexBuffer(meshBufferManager->GetIndexBuffer(), meshBufferManager->GetIndexType());

    commandBuffer.BindPipeline(&pipeline);

    renderFrame.SetShaderStorageBuffer(shader, 1, _transformBatch->TransformBuffer);
	renderFrame.SetShaderStorageBuffer(shader, 2, _instanceBuffer);

	renderFrame.SetShaderUniformBuffer(shader, 8,
		const_cast<GPUMaterialData*>(renderFrame.GetMaterialManager()->GetMaterialData()));
	renderFrame.SetShaderStorageBuffer(shader, 9, _materialIndexBuffer);

    if (shader.UsesBindlessTextures())
    {
        commandBuffer.BindBindlessDescriptorSet(
            renderFrame,
            pipeline.GetPipelineBindPoint(),
            shader.GetPipelineLayout());
    }

    if (perShader)
        perShader(shader);

    commandBuffer.BindDescriptorSets(renderFrame, VK_PIPELINE_BIND_POINT_GRAPHICS, shader);

    auto material = _materialBatches.begin()->second.Material.lock();
    perDraw(material);

    commandBuffer.DrawIndexedIndirect(
        indirectCommandBuffer,
        _indirectDrawBuffer.GetDrawCount(),
        static_cast<uint32_t>(IndirectDrawBuffer::GetDrawCommandSize())
    );
}

void Core::RendererBatch::DispatchFrustumOnlyCulling(RenderFrame& renderFrame,
    CommandBuffer& commandBuffer, const CameraBuffer& camera)
{
    auto* culler = renderFrame.GetOrCreateCuller(this, camera, _device, *_transformBatch);

    if (!culler->IsPrepared())
    {
        culler->Prepare(_device, _extents,
            _objectDataBuffer, _instanceBuffer, _indirectCommandBuffer,
            _instanceCount, _indirectDrawBuffer);
    }

    culler->DispatchFrustumOnlyCulling(renderFrame, commandBuffer, camera);
}
