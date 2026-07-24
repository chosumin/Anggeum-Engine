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

            auto shaderHandle = materials[i]->GetShaderHandle();
            if (!shaderHandle.IsValid())
                continue;

            // Skip non-geometry passes (Skybox, etc.)
            const string& pass = shaderHandle.Get().GetPass();
            if (pass != "Geometry")
                continue;

            AddMesh(entityId, materials[i], subMeshes[i]);
        }
    }

    CreateInstanceBuffer(_device);
    PrepareGPUDrivenRendering(extents);
}

Core::RendererBatch::~RendererBatch() = default;

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
        _indirectCommandBuffer,
        _indirectDrawBuffer.GetDrawCommands(), 0);
    jobs.push_back(&job);

    Core::VkBufferJob<uint32_t> job2(_device,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        _materialIndexBuffer,
        _indirectDrawBuffer.GetMaterialIndices(), 0);
    jobs.push_back(&job2);

    Core::VkBufferJob<GPUObjectData> job3(_device,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        _objectDataBuffer,
        objectData, 0);
    jobs.push_back(&job3);

    Core::CommandBuffer::ImmediateSubmit(_device, jobs);

    _extents = extents;
}

shared_ptr<Material> Core::RendererBatch::GetFirstMaterial() const
{
    if (_materialBatches.empty())
        return nullptr;
    return _materialBatches.begin()->second.Material.lock();
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

    _instanceBuffer = make_unique<Core::Buffer>(device, _instanceCount * sizeof(uint),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, MemoryType::DEVICE_LOCAL);

    Core::VkBufferJob<uint> job(device, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, _instanceBuffer, instanceData, true);
    Core::CommandBuffer::ImmediateSubmit(device, job);
}

