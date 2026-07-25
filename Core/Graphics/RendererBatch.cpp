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

Core::RendererBatch::RendererBatch(Device& device, Scene& scene, TransformBatch& transformBatch,
    RenderFrame& renderFrame, VkExtent2D extents)
    : _device(device)
    , _transformBatch(transformBatch)
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

            auto shaderHandle = materials[i].Get().GetShaderHandle();
            if (!shaderHandle.IsValid())
                continue;

            // Skip non-geometry passes (Skybox, etc.)
            const string& pass = shaderHandle.Get().GetPass();
            if (pass != "Geometry")
                continue;

            AddMesh(entityId, materials[i], subMeshes[i]);
        }
    }

    auto& frameResources = renderFrame.GetResources();
    CreateInstanceBuffer(frameResources);
    PrepareGPUDrivenRendering(frameResources, extents);
}

Core::RendererBatch::~RendererBatch() = default;

void Core::RendererBatch::AddMesh(uint entityId, Handle<Material> material, Handle<SubMesh> subMesh)
{
    auto* materialPtr = material.TryGet();
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

    auto* subMeshPtr = subMesh.TryGet();
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

void Core::RendererBatch::PrepareGPUDrivenRendering(FrameResources& frameResources, VkExtent2D extents)
{
    _indirectDrawBuffer.Clear();
    uint32_t globalFirstInstance = 0;
    uint32_t drawCommandIndex = 0;

    vector<GPUObjectData> objectData;
    objectData.reserve(_instanceCount);

    for (auto& [materialName, materialBatch] : _materialBatches)
    {
        auto* material = materialBatch.Material.TryGet();
        if (!material || !material->HasMaterialIndex())
            continue;

        uint32_t materialIndex = material->GetMaterialIndex();

        for (auto& [subMeshName, subMeshBatch] : materialBatch.SubMeshBatches)
        {
            auto* subMesh = subMeshBatch.SubMesh.TryGet();
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

    // Allocate the GPU-driven buffers from the frame's pool, then fill them via
    // copy jobs (the buffers are pool-owned, not created by the transfer job).
    const auto& drawCommands = _indirectDrawBuffer.GetDrawCommands();
    const auto& materialIndices = _indirectDrawBuffer.GetMaterialIndices();

    StorageBufferDesc indirectDesc{};
    indirectDesc.size = drawCommands.size() * sizeof(DrawIndexedIndirectCommand);
    indirectDesc.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    _indirectCommandBuffer = frameResources.GetOrCreateStorageBuffer("RendererBatch.IndirectCommand", indirectDesc);

    StorageBufferDesc materialDesc{};
    materialDesc.size = materialIndices.size() * sizeof(uint32_t);
    materialDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    _materialIndexBuffer = frameResources.GetOrCreateStorageBuffer("RendererBatch.MaterialIndex", materialDesc);

    StorageBufferDesc objectDesc{};
    objectDesc.size = objectData.size() * sizeof(GPUObjectData);
    objectDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    _objectDataBuffer = frameResources.GetOrCreateStorageBuffer("RendererBatch.ObjectData", objectDesc);

    vector<Job*> jobs;

    Core::VkBufferCopyJob<DrawIndexedIndirectCommand> job(_device,
        _indirectCommandBuffer.Get(), vector<DrawIndexedIndirectCommand>(drawCommands), 0);
    jobs.push_back(&job);

    Core::VkBufferCopyJob<uint32_t> job2(_device,
        _materialIndexBuffer.Get(), vector<uint32_t>(materialIndices), 0);
    jobs.push_back(&job2);

    Core::VkBufferCopyJob<GPUObjectData> job3(_device,
        _objectDataBuffer.Get(), move(objectData), 0);
    jobs.push_back(&job3);

    Core::CommandBuffer::ImmediateSubmit(_device, jobs);

    _extents = extents;
}

void Core::RendererBatch::CreateInstanceBuffer(FrameResources& frameResources)
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

    StorageBufferDesc desc{};
    desc.size = _instanceCount * sizeof(uint);
    desc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    _instanceBuffer = frameResources.GetOrCreateStorageBuffer("RendererBatch.Instance", desc);

    Core::VkBufferCopyJob<uint> job(_device, _instanceBuffer.Get(), move(instanceData), 0);
    Core::CommandBuffer::ImmediateSubmit(_device, job);
}

