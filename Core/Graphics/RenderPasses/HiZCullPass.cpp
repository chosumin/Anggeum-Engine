#include "stdafx.h"
#include "HiZCullPass.h"
#include "ResolvePass.h"
#include "TerrainNodeListPass.h"
#include "Graphics/Terrain/TerrainSystem.h"
#include "Graphics/FrameCounter.h"
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
        _state->extent = screenExtent;

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

    _hiZShader = resourceManager.LoadShader("Shaders/hiZGenerate.comp.spv");
    _hiZPipeline = resourceManager.LoadComputePipeline("Shaders/hiZGenerate.comp.spv");

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
        EnsureHiZTexture(frameResources);

        _prevDepth = frameResources.GetPreviousDepthBuffer();
    }
    else
    {
        _resolvedDepth = builder.GetTexture(ResolvePass::RT_RESOLVED_DEPTH);
        builder.Read(_resolvedDepth, TextureAccess::SampledCompute);

        _hiZTexture = frameResources.GetRenderTarget(RT_HIZ);
    }

    SetupMeshes(builder, frameResources, renderFrame);
    SetupTerrain(builder, frameResources, *camera);
    if (!_meshActive && !_terrainActive)
        return;

    // Both phases rebuild and sample the pyramid with their own barriers.
    FGTexture hiZ = _phase == Phase::Cull1
        ? builder.ImportTexture(RT_HIZ, _hiZTexture)
        : builder.GetTexture(RT_HIZ);
    builder.WriteManual(hiZ, TextureAccess::SampledCompute);

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

    _cullData = frameResources.GetOrCreateUniformBuffer<GPUCullData>(
        _phase == Phase::Cull1 ? "OcclusionCull.Pass1CullData" : "OcclusionCull.Pass2CullData");

    _meshActive = true;
}

void HiZCullPass::SetupTerrain(FrameGraphBuilder& builder, FrameResources& frameResources,
    const PerspectiveCamera& camera)
{
    if (_phase == Phase::Cull1)
    {
        if (!builder.HasBuffer(TerrainNodeListPass::SB_NODE_LIST))
            return;

        _terrainInputs = _terrainCuller.SetupInputs(builder, frameResources);

        _terrainOutput.patchList = _terrainCuller.CreatePatchList(builder, SB_TERRAIN_PATCH_LIST);
        _terrainOutput.drawArgs = builder.GetBuffer(TerrainNodeListPass::SB_PATCH_DRAW_ARGS);
        builder.Write(_terrainOutput.drawArgs, BufferAccess::StorageComputeWrite);
        _terrainRejected = _terrainCuller.CreateRejectedList(builder, SB_TERRAIN_REJECTED);
        _terrainRejectedCount = _terrainCuller.CreateRejectedCount(builder, SB_TERRAIN_REJECTED_COUNT);
        _terrainOutput.rejectedList = _terrainRejected;
        _terrainOutput.rejectedCount = _terrainRejectedCount;

        // Stats feed: TerrainSystem owns the slots and reads them in OnGUI.
        uint32_t slot = uint32_t(FrameCounter::GetFrameNumber() % MAX_FRAMES_IN_FLIGHT);
        _terrainReadback = builder.ImportBuffer("Terrain.PatchCountReadback" + to_string(slot),
            _renderScene.GetTerrainSystem().GetPatchCountReadback(slot));
        builder.Write(_terrainReadback, BufferAccess::TransferDst);

        _terrainCullData = frameResources.GetOrCreateUniformBuffer<TerrainCullData>("Terrain.CullData");
    }
    else
    {
        if (!builder.HasBuffer(SB_TERRAIN_REJECTED))
            return;

        _terrainRejected = builder.GetBuffer(SB_TERRAIN_REJECTED);
        builder.Read(_terrainRejected, BufferAccess::StorageComputeRead);
        _terrainRejectedCount = builder.GetBuffer(SB_TERRAIN_REJECTED_COUNT);
        builder.Read(_terrainRejectedCount, BufferAccess::StorageComputeRead);
        
        // Pass 2 reads no node list but still needs the node descs.
        _terrainCuller.AcquireNodeDescs(frameResources);

        _terrainOutput = {};
        _terrainOutput.patchList = _terrainCuller.CreatePatchList(builder, SB_TERRAIN_PASS2_PATCH_LIST);
        _terrainOutput.drawArgs = _terrainCuller.CreateDrawArgs(builder, SB_TERRAIN_PASS2_DRAW_ARGS);

        _terrainCullData = frameResources.GetOrCreateUniformBuffer<TerrainCullData>("Terrain.Pass2CullData");
    }

    _terrainPush = _terrainCuller.BuildPush(camera);
    _terrainActive = true;
}

void HiZCullPass::EnsureHiZTexture(FrameResources& frameResources)
{
    uint32_t maxDim = std::max(_state->extent.width, _state->extent.height);
    _state->hiZMipLevels = static_cast<uint32_t>(std::floor(std::log2(maxDim))) + 1;

    // Hi-Z occlusion sampling must be point-filtered, so use a NEAREST sampler.
    auto samplerDesc = DEFAULT_SAMPLER;
    samplerDesc.minFilter = VK_FILTER_NEAREST;
    samplerDesc.magFilter = VK_FILTER_NEAREST;
    samplerDesc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    RenderTargetDesc hiZDesc{};
    hiZDesc.extent = _state->extent;
    hiZDesc.format = VK_FORMAT_R32_SFLOAT;
    hiZDesc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    hiZDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    hiZDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    hiZDesc.mipLevels = _state->hiZMipLevels;
    hiZDesc.sampler = _resourceManager.LoadSampler(samplerDesc);

    _hiZTexture = frameResources.GetOrCreateRenderTarget(RT_HIZ, hiZDesc);

    // The mip chain is bound per level while recording, and the two culling
    // passes record on different worker threads against this one texture, so
    // the views are created here (main thread) instead of lazily on a worker.
    Texture& hiZTexture = _hiZTexture.Get();
    for (uint32_t mip = 1; mip < _state->hiZMipLevels; ++mip)
        hiZTexture.GetImage().GetOrCreateImageView(mip);
}

void HiZCullPass::Execute(FrameGraphPassContext& context, CommandBuffer& commandBuffer)
{
    if (!_active)
        return;

    auto& renderFrame = context.GetRenderFrame();
    auto& batch = renderFrame.GetRendererBatch();
    auto& slot = _state->slots[&renderFrame.GetResources()];

    const bool first = _phase == Phase::Cull1;
    commandBuffer.BeginDebugMarker(first ? "Pass 1 Culling" : "Pass 2 Culling");

    // Pass 1's depth is null on the frames before any has been produced.
    PrepareHiZ(context, commandBuffer, slot,
        first ? _prevDepth.TryGet() : &context.GetTexture(_resolvedDepth));

    if (_meshActive)
    {
        CullMeshes(context, commandBuffer, batch, slot);

        commandBuffer.BeginDebugMarker("Compact Draw Commands");
        CompactDrawCommands(context, commandBuffer, batch);
        commandBuffer.EndDebugMarker();
    }

    if (_terrainActive)
    {
        commandBuffer.BeginDebugMarker("Terrain Patch Culling");
        CullTerrain(context, commandBuffer, slot);
        commandBuffer.EndDebugMarker();
    }

    commandBuffer.EndDebugMarker();
}

void HiZCullPass::PrepareHiZ(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
    SlotState& slot, Texture* depth)
{
    // Build the Hi-Z pyramid from the depth this phase was given. It is
    // absent on the frames before the first depth exists (pass 1 reads the
    // previous frame's), so the build is skipped rather than assumed.
    if (depth != nullptr)
    {
        BuildHiZ(context, commandBuffer, *depth);
        slot.hiZBuilt = true;
    }
    else if (!(slot.hiZImage == _hiZTexture))
    {
        // A pyramid this slot has not touched yet. Nothing has written it, but
        // the cull shader binds it regardless, so move it out of UNDEFINED —
        // and it holds no depth, so occlusion testing stays off.
        commandBuffer.CreateBarrierBatch()
            .Image(_hiZTexture.Get(),
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            .Submit();

        slot.hiZBuilt = false;
    }
    slot.hiZImage = _hiZTexture;
}

void HiZCullPass::CullMeshes(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
    RendererBatch& batch, SlotState& slot)
{
    // Zero this phase's counters ahead of the dispatches.
    {
        commandBuffer.FillBuffer(context.GetBuffer(_counts), 0, VK_WHOLE_SIZE, 0);
        commandBuffer.FillBuffer(context.GetBuffer(_visibleDrawCount), 0, VK_WHOLE_SIZE, 0);
        if (_phase == Phase::Cull1)
            commandBuffer.FillBuffer(context.GetBuffer(_rejectedCount), 0, VK_WHOLE_SIZE, 0);

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
        barriers.Submit();
    }

    // Culling parameters. Built CPU-side and assigned once: the mapping is
    // uncached, so field-by-field writes into it would be slow.
    const CameraBuffer& camera = _state->camera;

    GPUCullData cullData{};
    cullData.view = camera.View;
    cullData.proj = camera.Projection;
    cullData.screenSize = glm::vec2(_state->extent.width, _state->extent.height);
    cullData.drawCount = batch.GetInstanceCount();
    cullData.hiZMipLevels = _state->hiZMipLevels;
    cullData.enableOcclusionCulling = slot.hiZBuilt ? 1 : 0;

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
    builder.SetTextureBuffer(5, _hiZTexture.Get());
    builder.SetStorageBuffer(6, context.GetBuffer(_counts));
    builder.SetStorageBuffer(10, context.GetBuffer(_rejectedIndices));
    builder.SetStorageBuffer(11, context.GetBuffer(_rejectedCount));
    auto& resources = builder.Build();

    commandBuffer.BindDescriptorSet(_cullPipeline.Get().GetPipelineBindPoint(),
        cullShader, resources);

    uint32_t groupCount = (batch.GetInstanceCount() + 63) / 64;
    commandBuffer.Dispatch(groupCount, 1, 1);
}

void HiZCullPass::CullTerrain(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
    SlotState& slot)
{
    const TerrainSystem& terrain = _renderScene.GetTerrainSystem();
    const TerrainConfig& config = terrain.GetConfig();
    const CameraBuffer& camera = _state->camera;
    const bool first = _phase == Phase::Cull1;

    TerrainCullData cullData{};
    cullData.view = camera.View;
    cullData.proj = camera.Projection;
    Math::ExtractFrustumPlanes(camera.Projection * camera.View, cullData.frustumPlanes);
    cullData.screenHiZ = vec4(float(_state->extent.width), float(_state->extent.height),
        float(_state->hiZMipLevels), slot.hiZBuilt ? 1.0f : 0.0f);
    // Same conservative pad the CPU culler uses for decimated coarse bakes.
    cullData.heightBounds = vec4(config.heightMin, config.heightMax, 2.0f, 0.0f);
    cullData.debug = uvec4(first && terrain.IsCullingBypassed() ? 1u : 0u, 0u, 0u, 0u);

    Buffer& cullDataBuffer = _terrainCullData.Get();
    cullDataBuffer.Update(cullData);

    Texture* hiZ = slot.hiZBuilt ? &_hiZTexture.Get() : nullptr;

    if (first)
    {
        Buffer& rejectedCount = context.GetBuffer(_terrainRejectedCount);
        commandBuffer.FillBuffer(rejectedCount, 0, VK_WHOLE_SIZE, 0);
        commandBuffer.CreateBarrierBatch()
            .Buffer(rejectedCount,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)
            .Submit();

        _terrainCuller.Dispatch(context, commandBuffer, _terrainInputs, _terrainOutput,
            cullDataBuffer, hiZ, _terrainPush);

        // Stats readback of instanceCount (offset 4 in the draw args).
        Buffer& drawArgs = context.GetBuffer(_terrainOutput.drawArgs);
        commandBuffer.CreateBarrierBatch()
            .Buffer(drawArgs,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT)
            .Submit();
        commandBuffer.CopyBuffer(drawArgs, context.GetBuffer(_terrainReadback),
            0, sizeof(uint32_t), sizeof(uint32_t));
    }
    else
    {
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
}

void HiZCullPass::CompactDrawCommands(FrameGraphPassContext& context,
    CommandBuffer& commandBuffer, RendererBatch& batch)
{
    // Same-pass hazard: the cull dispatch above wrote this phase's counts.
    commandBuffer.CreateBarrierBatch()
        .Buffer(context.GetBuffer(_counts),
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
    builder.SetStorageBuffer(1, context.GetBuffer(_counts));
    builder.SetStorageBuffer(2, context.GetBuffer(_visibleCommands));
    builder.SetStorageBuffer(3, context.GetBuffer(_visibleDrawCount));
    auto& resources = builder.Build();

    commandBuffer.BindDescriptorSet(_compactPipeline.Get().GetPipelineBindPoint(),
        compactShader, resources);
    commandBuffer.PushConstants(compactShader, 0, drawCount);

    commandBuffer.Dispatch(std::max(1u, (drawCount + 63) / 64), 1, 1);
}

void HiZCullPass::BuildHiZ(FrameGraphPassContext& context, CommandBuffer& commandBuffer,
    Texture& depth)
{
    Texture& hiZTex = _hiZTexture.Get();

    // Prepare depth as copy source and Hi-Z as copy destination.
    commandBuffer.CreateBarrierBatch()
        .Image(depth,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
        .Image(hiZTex,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        .Submit();

    // Copy resolved depth to Hi-Z mip 0
    commandBuffer.CopyImage(depth, hiZTex, 0, 0, 0, 0);

    // Restore depth for sampling and move Hi-Z to GENERAL for the mip chain.
    commandBuffer.CreateBarrierBatch()
        .Image(depth,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .Image(hiZTex,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL)
        .Submit();

    if (_state->hiZMipLevels > 1)
    {
        commandBuffer.BindPipeline(&_hiZPipeline.Get());

        uint32_t mipWidth = _state->extent.width;
        uint32_t mipHeight = _state->extent.height;

        for (uint32_t mip = 1; mip < _state->hiZMipLevels; ++mip)
        {
            mipWidth = std::max(1u, mipWidth / 2);
            mipHeight = std::max(1u, mipHeight / 2);

            auto& hiZShader = _hiZShader.Get();
            auto builder = context.CreateDescriptorSetBuilder(hiZShader, 0);
            builder.SetTextureBuffer(0, _hiZTexture.Get(), mip - 1, VK_IMAGE_LAYOUT_GENERAL);
            builder.SetTextureBuffer(1, _hiZTexture.Get(), mip, VK_IMAGE_LAYOUT_GENERAL);
            auto& resources = builder.Build();

            commandBuffer.BindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, hiZShader,
                resources);

            struct HiZPushConstants {
                int32_t outputWidth;
                int32_t outputHeight;
            } hiZPc = { static_cast<int32_t>(mipWidth), static_cast<int32_t>(mipHeight) };

            commandBuffer.PushConstants(hiZShader, 0, hiZPc);

            commandBuffer.Dispatch((mipWidth + 7) / 8, (mipHeight + 7) / 8, 1);

            commandBuffer.CreateBarrierBatch()
                .Image(hiZTex,
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_GENERAL)
                .Submit();
        }
    }

    // Hi-Z: GENERAL -> SHADER_READ_ONLY for the cull dispatch that samples it.
    commandBuffer.CreateBarrierBatch()
        .Image(hiZTex,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .Submit();
}
