#include "stdafx.h"
#include "HiZCullPass.h"
#include "HiZPyramid.h"
#include "ResolvePass.h"
#include "TerrainPatchCullPass.h"
#include "Graphics/Terrain/TerrainSystem.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/FrameGraph/FrameGraphBuilder.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/FrameResources.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/ResourceManager.h"
#include "Utils/Math.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Graphics/Vulkans/Texture.h"
#include "Foundation/Scene.h"
#include "Components/PerspectiveCamera.h"

using namespace Core;

HiZCullPass::HiZCullPass(Device& device, ResourceManager& resourceManager, RenderScene& renderScene,
    TerrainPatchCuller& terrainCuller, VkExtent2D screenExtent, Phase phase, HiZCullPass* cull1)
    : _device(device)
    , _resourceManager(resourceManager)
    , _renderScene(renderScene)
    , _terrainCuller(terrainCuller)
    , _phase(phase)
{
    if (_phase == Phase::Cull1)
    {
        assert(cull1 == nullptr && "Cull1 owns the shared state");
        _state = make_shared<SharedState>();
        _state->pyramid = make_unique<HiZPyramid>(resourceManager, screenExtent);

        _cullShader = resourceManager.LoadShader("Shaders/gpuCulling.comp.spv");
        _cullPipeline = resourceManager.LoadComputePipeline("Shaders/gpuCulling.comp.spv");
    }
    else
    {
        assert(cull1 != nullptr && cull1->_phase == Phase::Cull1 &&
            "Cull2 shares Cull1's state");
        _state = cull1->_state;

        _cullShader = resourceManager.LoadShader("Shaders/gpuCullingPass2.comp.spv");
        _cullPipeline = resourceManager.LoadComputePipeline("Shaders/gpuCullingPass2.comp.spv");
    }

    _compactShader = resourceManager.LoadShader("Shaders/compactDrawCommands.comp.spv");
    _compactPipeline = resourceManager.LoadComputePipeline("Shaders/compactDrawCommands.comp.spv");
}

HiZCullPass::~HiZCullPass() = default;

void HiZCullPass::Setup(FrameGraphBuilder& builder, FrameResources& frameResources,
    RenderFrame& renderFrame)
{
    _active = false;
    _meshActive = false;
    _terrainActive = false;

    PerspectiveCamera* camera = _renderScene.GetScene().GetMainCamera();
    if (!camera)
        return; // declares nothing: the pass culls itself this frame

    if (_phase == Phase::Cull1)
    {
        _state->camera = camera->Matrices;
        _hiZTexture = _state->pyramid->EnsureTexture(frameResources, RT_HIZ);
        // Stale after a resize recreates the targets: treat as not built.
        _sampledHiZ = frameResources.GetPreviousHiZ();
        if (_sampledHiZ.TryGet() == nullptr)
            _sampledHiZ = Handle<Texture>{};
    }
    else
    {
        _resolvedDepth = builder.GetTexture(ResolvePass::RT_RESOLVED_DEPTH);
        _hiZTexture = frameResources.GetRenderTarget(RT_HIZ);
        _sampledHiZ = _hiZTexture;
    }

    SetupMeshes(builder, frameResources, renderFrame);
    if (_phase == Phase::Cull2)
        SetupTerrain(builder, frameResources, *camera);
    if (!_meshActive && !_terrainActive)
        return;

    if (_phase == Phase::Cull1)
    {
        if (_sampledHiZ.IsValid())
        {
            FGTexture prevHiZ = builder.ImportTexture("OcclusionCull.PrevHiZ", _sampledHiZ);
            builder.Read(prevHiZ, TextureAccess::SampledCompute);
        }
    }
    else
    {
        // The pyramid copies the depth, then the build transitions the pyramid itself.
        builder.Read(_resolvedDepth, TextureAccess::TransferSrc);
        FGTexture hiZ = builder.ImportTexture(RT_HIZ, _hiZTexture);
        builder.WriteManual(hiZ, TextureAccess::SampledCompute);
    }

    _active = true;
}

void HiZCullPass::SetupMeshes(FrameGraphBuilder& builder, FrameResources& frameResources,
    RenderFrame& renderFrame)
{
    auto& batch = renderFrame.GetRendererBatch();
    if (batch.GetDrawCommandCount() == 0)
        return;

    // Change only when the tables grow, so transients sized by them are not
    // rebuilt on every load or unload.
    const uint32_t drawCapacity = batch.GetDrawCommandCapacity();
    const uint32_t instanceCapacity = batch.GetInstanceCapacity();

    FGBufferDesc instanceIDsDesc{ instanceCapacity * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
    FGBufferDesc countsDesc{ drawCapacity * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT };
    FGBufferDesc visibleDesc{ drawCapacity * sizeof(DrawIndexedIndirectCommand),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
    FGBufferDesc drawCountDesc{ sizeof(uint32_t),
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT };

    if (_phase == Phase::Cull1)
    {
        _counts = builder.CreateBuffer(SB_PASS1_COUNTS, countsDesc);
        builder.Write(_counts, BufferAccess::FillComputeWrite);

        _instanceIDs = builder.CreateBuffer(SB_PASS1_INSTANCE_IDS, instanceIDsDesc);
        builder.Write(_instanceIDs, BufferAccess::StorageComputeWrite);

        _visibleCommands = builder.CreateBuffer(SB_PASS1_INDIRECT, visibleDesc);
        builder.Write(_visibleCommands, BufferAccess::StorageComputeWrite);

        _visibleDrawCount = builder.CreateBuffer(SB_PASS1_DRAW_COUNT, drawCountDesc);
        builder.Write(_visibleDrawCount, BufferAccess::FillComputeWrite);

        _rejectedIndices = builder.CreateBuffer(SB_REJECTED_INDICES,
            { instanceCapacity * sizeof(uint32_t), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT });
        builder.Write(_rejectedIndices, BufferAccess::StorageComputeWrite);

        _rejectedCount = builder.CreateBuffer(SB_REJECTED_COUNT,
            { sizeof(uint32_t),
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT });
        builder.Write(_rejectedCount, BufferAccess::FillComputeWrite);
    }
    else
    {
        // Cull1 declared nothing this frame (it runs first): nothing to recover.
        if (!builder.HasBuffer(SB_PASS1_INDIRECT))
            return;

        _rejectedIndices = builder.GetBuffer(SB_REJECTED_INDICES);
        builder.Read(_rejectedIndices, BufferAccess::StorageComputeRead);

        _rejectedCount = builder.GetBuffer(SB_REJECTED_COUNT);
        builder.Read(_rejectedCount, BufferAccess::StorageComputeRead);

        _counts = builder.CreateBuffer(SB_PASS2_COUNTS, countsDesc);
        builder.Write(_counts, BufferAccess::FillComputeWrite);

        _instanceIDs = builder.CreateBuffer(SB_PASS2_INSTANCE_IDS, instanceIDsDesc);
        builder.Write(_instanceIDs, BufferAccess::StorageComputeWrite);

        _visibleCommands = builder.CreateBuffer(SB_PASS2_INDIRECT, visibleDesc);
        builder.Write(_visibleCommands, BufferAccess::StorageComputeWrite);

        _visibleDrawCount = builder.CreateBuffer(SB_PASS2_DRAW_COUNT, drawCountDesc);
        builder.Write(_visibleDrawCount, BufferAccess::FillComputeWrite);
    }

    _transparentCounts = FGBuffer{};
    _transparentInstanceIDs = FGBuffer{};
    _transparentCommands = FGBuffer{};
    _transparentDrawCount = FGBuffer{};
    if (batch.HasTransparentInstances())
    {
        const bool first = _phase == Phase::Cull1;
        _transparentCounts = builder.CreateBuffer(
            first ? SB_PASS1_TRANSPARENT_COUNTS : SB_PASS2_TRANSPARENT_COUNTS, countsDesc);
        builder.Write(_transparentCounts, BufferAccess::FillComputeWrite);

        _transparentInstanceIDs = builder.CreateBuffer(
            first ? SB_PASS1_TRANSPARENT_INSTANCE_IDS : SB_PASS2_TRANSPARENT_INSTANCE_IDS, instanceIDsDesc);
        builder.Write(_transparentInstanceIDs, BufferAccess::StorageComputeWrite);

        _transparentCommands = builder.CreateBuffer(
            first ? SB_PASS1_TRANSPARENT_INDIRECT : SB_PASS2_TRANSPARENT_INDIRECT, visibleDesc);
        builder.Write(_transparentCommands, BufferAccess::StorageComputeWrite);

        _transparentDrawCount = builder.CreateBuffer(
            first ? SB_PASS1_TRANSPARENT_DRAW_COUNT : SB_PASS2_TRANSPARENT_DRAW_COUNT, drawCountDesc);
        builder.Write(_transparentDrawCount, BufferAccess::FillComputeWrite);
    }

    _cullData = frameResources.GetOrCreateUniformBuffer<GPUCullData>(
        _phase == Phase::Cull1 ? "OcclusionCull.Pass1CullData" : "OcclusionCull.Pass2CullData");

    _meshActive = true;
}

void HiZCullPass::SetupTerrain(FrameGraphBuilder& builder, FrameResources& frameResources,
    const PerspectiveCamera& camera)
{
    // Pass 1 culled on the dedicated terrain cull pass; 
    // nothing to recover when it declared nothing this frame.
    if (!builder.HasBuffer(TerrainPatchCullPass::SB_REJECTED))
        return;

    _terrainRejected = builder.GetBuffer(TerrainPatchCullPass::SB_REJECTED);
    builder.Read(_terrainRejected, BufferAccess::StorageComputeRead);
    _terrainRejectedCount = builder.GetBuffer(TerrainPatchCullPass::SB_REJECTED_COUNT);
    builder.Read(_terrainRejectedCount, BufferAccess::StorageComputeRead);

    // Pass 2 reads no node list but still needs the node descs.
    _terrainCuller.AcquireNodeDescs(frameResources);

    _terrainOutput = {};
    _terrainOutput.patchList = _terrainCuller.CreatePatchList(builder, SB_TERRAIN_PASS2_PATCH_LIST);
    _terrainOutput.drawArgs = _terrainCuller.CreateDrawArgs(builder, SB_TERRAIN_PASS2_DRAW_ARGS);

    _terrainCullData = frameResources.GetOrCreateUniformBuffer<TerrainCullData>("Terrain.Pass2CullData");

    _terrainPush = _terrainCuller.BuildPush(camera);
    _terrainActive = true;
}

void HiZCullPass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
    if (!_active)
        return;

    auto& renderFrame = context.GetRenderFrame();
    auto& batch = renderFrame.GetRendererBatch();

    const bool first = _phase == Phase::Cull1;
    commandBuffer.BeginDebugMarker(first ? "Pass 1 Culling" : "Pass 2 Culling");

    if (!first)
        _state->pyramid->Build(context, commandBuffer,
            context.GetTexture(_resolvedDepth), _hiZTexture.Get());

    if (_meshActive)
    {
        CullMeshes(context, commandBuffer, batch);

        commandBuffer.BeginDebugMarker("Compact Draw Commands");
        CompactDrawCommands(context, commandBuffer, batch,
            _counts, _visibleCommands, _visibleDrawCount);
        if (_transparentCounts.IsValid())
            CompactDrawCommands(context, commandBuffer, batch,
                _transparentCounts, _transparentCommands, _transparentDrawCount);
        commandBuffer.EndDebugMarker();
    }

    if (_terrainActive)
    {
        commandBuffer.BeginDebugMarker("Terrain Patch Culling");
        CullTerrain(context, commandBuffer);
        commandBuffer.EndDebugMarker();
    }

    commandBuffer.EndDebugMarker();
}

void HiZCullPass::CullMeshes(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
    RendererBatch& batch)
{
    // Zero this phase's counters ahead of the dispatches.
    {
        commandBuffer.FillBuffer(context.GetBuffer(_counts), 0, VK_WHOLE_SIZE, 0);
        commandBuffer.FillBuffer(context.GetBuffer(_visibleDrawCount), 0, VK_WHOLE_SIZE, 0);
        if (_phase == Phase::Cull1)
            commandBuffer.FillBuffer(context.GetBuffer(_rejectedCount), 0, VK_WHOLE_SIZE, 0);
        if (_transparentCounts.IsValid())
        {
            commandBuffer.FillBuffer(context.GetBuffer(_transparentCounts), 0, VK_WHOLE_SIZE, 0);
            commandBuffer.FillBuffer(context.GetBuffer(_transparentDrawCount), 0, VK_WHOLE_SIZE, 0);
        }

        auto barriers = commandBuffer.CreateBarrierBatch();
        barriers.Buffer(context.GetBuffer(_counts),
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        barriers.Buffer(context.GetBuffer(_visibleDrawCount),
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        if (_phase == Phase::Cull1)
            barriers.Buffer(context.GetBuffer(_rejectedCount),
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        if (_transparentCounts.IsValid())
        {
            barriers.Buffer(context.GetBuffer(_transparentCounts),
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            barriers.Buffer(context.GetBuffer(_transparentDrawCount),
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        }
        barriers.Submit();
    }

    // Culling parameters. Built CPU-side and assigned once: the mapping is
    // uncached, so field-by-field writes into it would be slow.
    const CameraBuffer& camera = _state->camera;
    const HiZPyramid& pyramid = *_state->pyramid;
    const bool occlusion = _sampledHiZ.IsValid();

    GPUCullData cullData{};
    cullData.view = camera.View;
    cullData.proj = camera.Projection;
    cullData.screenSize = glm::vec2(pyramid.GetExtent().width, pyramid.GetExtent().height);
    cullData.drawCount = batch.GetInstanceCount();
    cullData.hiZMipLevels = pyramid.GetMipLevels();
    cullData.enableOcclusionCulling = occlusion ? 1 : 0;

    glm::mat4 viewProj = camera.Projection * camera.View;
    Math::ExtractFrustumPlanes(viewProj, cullData.frustumPlanes);

    Buffer& cullDataBuffer = _cullData.Get();
    cullDataBuffer.Update(cullData);

    commandBuffer.BindPipeline(&_cullPipeline.Get());

    auto& cullShader = _cullShader.Get();
    auto builder = context.CreateDescriptorSetBuilder(cullShader, 0);
    builder.SetUniformBuffer(0, cullDataBuffer);
    builder.SetStorageBuffer(1, batch.GetInstanceDataBuffer());
    builder.SetStorageBuffer(2, batch.GetTransformBuffer());
    builder.SetStorageBuffer(3, context.GetBuffer(_instanceIDs));
    builder.SetStorageBuffer(4, batch.GetIndirectCommandBuffer());
    builder.SetTextureBuffer(5, occlusion ? _sampledHiZ.Get() : _hiZTexture.Get());
    builder.SetStorageBuffer(6, context.GetBuffer(_counts));
    const bool transparent = _transparentCounts.IsValid();
    builder.SetStorageBuffer(7, context.GetBuffer(transparent ? _transparentCounts : _counts));
    builder.SetStorageBuffer(8, context.GetBuffer(transparent ? _transparentInstanceIDs : _instanceIDs));
    builder.SetStorageBuffer(10, context.GetBuffer(_rejectedIndices));
    builder.SetStorageBuffer(11, context.GetBuffer(_rejectedCount));
    auto& resources = builder.Build();

    commandBuffer.BindDescriptorSet(_cullPipeline.Get().GetPipelineBindPoint(),
        cullShader, resources);

    uint32_t groupCount = (batch.GetInstanceCount() + 63) / 64;
    commandBuffer.Dispatch(groupCount, 1, 1);
}

void HiZCullPass::CullTerrain(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
    const TerrainConfig& config = _renderScene.GetTerrainSystem().GetConfig();
    const CameraBuffer& camera = _state->camera;
    const HiZPyramid& pyramid = *_state->pyramid;

    TerrainCullData cullData{};
    cullData.view = camera.View;
    cullData.proj = camera.Projection;
    Math::ExtractFrustumPlanes(camera.Projection * camera.View, cullData.frustumPlanes);
    cullData.screenHiZ = vec4(float(pyramid.GetExtent().width), float(pyramid.GetExtent().height),
        float(pyramid.GetMipLevels()), 1.0f);
    // Same conservative pad the CPU culler uses for decimated coarse bakes.
    cullData.heightBounds = vec4(config.heightMin, config.heightMax, 2.0f, 0.0f);

    Buffer& cullDataBuffer = _terrainCullData.Get();
    cullDataBuffer.Update(cullData);

    Buffer& drawArgs = context.GetBuffer(_terrainOutput.drawArgs);
    _terrainCuller.ResetDrawArgs(commandBuffer, drawArgs);
    commandBuffer.CreateBarrierBatch()
        .Buffer(drawArgs,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)
        .Submit();

    _terrainCuller.DispatchPass2(context, commandBuffer, _terrainRejected, _terrainRejectedCount,
        _terrainOutput, cullDataBuffer, _hiZTexture.Get(), _terrainPush);
}

void HiZCullPass::CompactDrawCommands(FrameGraphPassContext& context,
    CommandBuffer& commandBuffer, RendererBatch& batch,
    FGBuffer counts, FGBuffer outCommands, FGBuffer outDrawCount)
{
    // Same-pass hazard: the cull dispatch above wrote this phase's counts.
    commandBuffer.CreateBarrierBatch()
        .Buffer(context.GetBuffer(counts),
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT)
        .Submit();

    const uint32_t drawCount = batch.GetDrawCommandCount();

    commandBuffer.BindPipeline(&_compactPipeline.Get());

    auto& compactShader = _compactShader.Get();
    auto builder = context.CreateDescriptorSetBuilder(compactShader, 0);
    builder.SetStorageBuffer(0, batch.GetIndirectCommandBuffer());
    builder.SetStorageBuffer(1, context.GetBuffer(counts));
    builder.SetStorageBuffer(2, context.GetBuffer(outCommands));
    builder.SetStorageBuffer(3, context.GetBuffer(outDrawCount));
    auto& resources = builder.Build();

    commandBuffer.BindDescriptorSet(_compactPipeline.Get().GetPipelineBindPoint(),
        compactShader, resources);
    commandBuffer.PushConstants(compactShader, 0, drawCount);

    commandBuffer.Dispatch(std::max(1u, (drawCount + 63) / 64), 1, 1);
}
