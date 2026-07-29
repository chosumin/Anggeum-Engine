#include "Graphics/OcclusionCuller.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/ResourceCache.h"
#include "Vulkans/Texture.h"

using namespace Core;

Core::OcclusionCuller::OcclusionCuller(Device& device, RenderFrame& renderFrame,
    RendererBatch& rendererBatch, uint32_t id)
    : Culler(device, rendererBatch, id)
{
    auto& frameResources = renderFrame.GetResources();
    auto& resourceCache = device.GetResourceCache();

    PrepareHiZResources(device, renderFrame, rendererBatch.GetExtents());

    _cullingShader = resourceCache.LoadShader("Shaders/gpuCulling.comp.spv");
    _cullingPipeline = make_unique<Pipeline>(device, _cullingShader.Get());

    _pass2CullingShader = resourceCache.LoadShader("Shaders/gpuCullingPass2.comp.spv");
    _pass2CullingPipeline = make_unique<Pipeline>(device, _pass2CullingShader.Get());

    _resetDrawCommandsShader = resourceCache.LoadShader("Shaders/resetDrawCommands.comp.spv");
    _resetDrawCommandsPipeline = make_unique<Pipeline>(device, _resetDrawCommandsShader.Get());

    _pass1CullDataBuffer = frameResources.GetOrCreateUniformBuffer<GPUCullData>(_namePrefix + "Pass1CullData");
    _pass2CullDataBuffer = frameResources.GetOrCreateUniformBuffer<GPUCullData>(_namePrefix + "Pass2CullData");

    BufferDesc rejectedCountDesc{};
    rejectedCountDesc.size = sizeof(uint32_t);
    rejectedCountDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    rejectedCountDesc.memoryType = MemoryType::DEVICE_LOCAL;
    _rejectedCountBuffer = frameResources.GetOrCreateStorageBuffer(_namePrefix + "RejectedCount", rejectedCountDesc);

    PrepareBatchResources(renderFrame);
}

void Core::OcclusionCuller::PrepareBatchResources(RenderFrame& renderFrame)
{
    Culler::PrepareBatchResources(renderFrame);

    auto& frameResources = renderFrame.GetResources();

    BufferDesc rejectedIndicesDesc{};
    rejectedIndicesDesc.size = _instanceCount * sizeof(uint32_t);
    rejectedIndicesDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    rejectedIndicesDesc.memoryType = MemoryType::DEVICE_LOCAL;
    _rejectedIndicesBuffer = frameResources.CreateOrReplaceStorageBuffer(
        _namePrefix + "RejectedIndices", rejectedIndicesDesc);

    // Filled for the same reason as the base pass-1 buffer: the pass-2 culling
    // shader only rewrites instance counts, the rest of each command comes from
    // this fill.
    const auto& drawCommands = _rendererBatch.GetIndirectDrawBuffer().GetDrawCommands();

    BufferDesc indirectDesc{};
    indirectDesc.size = drawCommands.size() * sizeof(DrawIndexedIndirectCommand);
    indirectDesc.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    indirectDesc.memoryType = MemoryType::DEVICE_LOCAL;

    _pass2IndirectCommandBuffer = frameResources.CreateOrReplaceStorageBuffer(
        _namePrefix + "Pass2Indirect", indirectDesc, drawCommands);
}

void Core::OcclusionCuller::ResetDrawCommands(RenderFrame& renderFrame, CommandBuffer& commandBuffer)
{
    uint32_t drawCount = _drawCount;

    commandBuffer.BindPipeline(_resetDrawCommandsPipeline.get());

    auto& resetShader = _resetDrawCommandsShader.Get();
    auto builder = renderFrame.GetResources().CreateDescriptorSetBuilder(resetShader, 0);
    builder.SetStorageBuffer(0, _indirectCommandBuffer.Get());
    builder.SetStorageBuffer(1, _pass2IndirectCommandBuffer.Get());
    builder.SetStorageBuffer(2, _rejectedCountBuffer.Get());
    auto& resources = builder.Build();

    commandBuffer.PushConstants(resetShader, 0, drawCount);
    commandBuffer.BindDescriptorSet(_resetDrawCommandsPipeline->GetPipelineBindPoint(),
        resetShader, resources);

    uint32_t groupCount = (drawCount + 63) / 64;
    commandBuffer.Dispatch(std::max(1u, groupCount), 1, 1);

    commandBuffer.CreateBarrierBatch()
        .Buffer(_indirectCommandBuffer.Get(),
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)
        .Buffer(_pass2IndirectCommandBuffer.Get(),
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)
        .Buffer(_rejectedCountBuffer.Get(),
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)
        .Submit();
}

void Core::OcclusionCuller::DispatchPass1Culling(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
    const CameraBuffer& camera, Handle<Texture> depth)
{
    DispatchCulling(renderFrame, commandBuffer, camera, depth,
        _indirectCommandBuffer.Get(), _pass1CullDataBuffer.Get(),
        _cullingShader.Get(), _cullingPipeline.get());
}

void Core::OcclusionCuller::DispatchPass2Culling(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
    const CameraBuffer& camera, Handle<Texture> depth)
{
    DispatchCulling(renderFrame, commandBuffer, camera, depth,
        _pass2IndirectCommandBuffer.Get(), _pass2CullDataBuffer.Get(),
        _pass2CullingShader.Get(), _pass2CullingPipeline.get());
}

void Core::OcclusionCuller::DispatchCulling(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
    const CameraBuffer& camera, Handle<Texture> depth,
    Core::Buffer& indirectCommandBuffer, Core::Buffer& cullDataBuffer,
    Shader& cullingShader, Pipeline* cullingPipeline)
{
    // Generate Hi-Z from depth
    if (!_hiZInitialized)
    {
        commandBuffer.CreateBarrierBatch()
            .Image(_hiZTexture.Get(),
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            .Submit();
        _hiZInitialized = true;
    }
    else
    {
        GenerateHiZBuffer(renderFrame, commandBuffer, depth);
    }

    // Culling dispatch. Built CPU-side and assigned once: the mapping is
    // uncached, so field-by-field writes into it would be slow.
    GPUCullData cullData{};
    cullData.view = camera.View;
    cullData.proj = camera.Projection;
    cullData.screenSize = glm::vec2(_screenExtent.width, _screenExtent.height);
    cullData.drawCount = _instanceCount;
    cullData.hiZMipLevels = _hiZMipLevels;
    cullData.enableOcclusionCulling = _hiZInitialized ? 1 : 0;

    glm::mat4 viewProj = camera.Projection * camera.View;
    ExtractFrustumPlanes(viewProj, cullData.frustumPlanes);

    cullDataBuffer.Update(cullData);

    commandBuffer.BindPipeline(cullingPipeline);

    auto builder = renderFrame.GetResources().CreateDescriptorSetBuilder(cullingShader, 0);
    builder.SetUniformBuffer(0, cullDataBuffer);
    builder.SetStorageBuffer(1, _rendererBatch.GetObjectDataBuffer());
    builder.SetStorageBuffer(2, _rendererBatch.GetTransformBatch().TransformBuffer.Get());
    builder.SetStorageBuffer(3, _rendererBatch.GetInstanceBuffer());
    builder.SetStorageBuffer(4, indirectCommandBuffer);
    builder.SetTextureBuffer(5, _hiZTexture);
    builder.SetStorageBuffer(10, _rejectedIndicesBuffer.Get());
    builder.SetStorageBuffer(11, _rejectedCountBuffer.Get());
    auto& resources = builder.Build();

    commandBuffer.BindDescriptorSet(cullingPipeline->GetPipelineBindPoint(),
        cullingShader, resources);

    uint32_t groupCount = (_instanceCount + 63) / 64;
    commandBuffer.Dispatch(groupCount, 1, 1);

    commandBuffer.CreateBarrierBatch()
        .Buffer(indirectCommandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT)
        .Buffer(_rendererBatch.GetInstanceBuffer(),
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT)
        .Buffer(_rejectedIndicesBuffer.Get(),
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT)
        .Buffer(_rejectedCountBuffer.Get(),
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT)
        .Submit();
}

void Core::OcclusionCuller::PrepareHiZResources(Device& device, RenderFrame& renderFrame, VkExtent2D extents)
{
    _screenExtent = extents;

    // Calculate mip levels
    uint32_t maxDim = std::max(_screenExtent.width, _screenExtent.height);
    _hiZMipLevels = static_cast<uint32_t>(std::floor(std::log2(maxDim))) + 1;

    // Hi-Z occlusion sampling must be point-filtered, so use a NEAREST sampler.
    auto samplerDesc = DEFAULT_SAMPLER;
    samplerDesc.minFilter = VK_FILTER_NEAREST;
    samplerDesc.magFilter = VK_FILTER_NEAREST;
    samplerDesc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    RenderTargetDesc hiZDesc{};
    hiZDesc.extent = _screenExtent;
    hiZDesc.format = VK_FORMAT_R32_SFLOAT;
    hiZDesc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    hiZDesc.samples = VK_SAMPLE_COUNT_1_BIT;
    hiZDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    hiZDesc.mipLevels = _hiZMipLevels;
    hiZDesc.sampler = device.GetResourceCache().LoadSampler(samplerDesc);

    _hiZTexture = renderFrame.GetResources().GetOrCreateRenderTarget(_namePrefix + "HiZ", hiZDesc);

    // Load shaders
    _hiZGenerateShader = device.GetResourceCache().LoadShader("Shaders/hiZGenerate.comp.spv");
    _hiZPipeline = make_unique<Pipeline>(device, _hiZGenerateShader.Get());
}

void Core::OcclusionCuller::GenerateHiZBuffer(RenderFrame& renderFrame, CommandBuffer& commandBuffer, Handle<Texture> depth)
{
    // Resolve once on the render thread; the image ops take Texture& (the mip loop
    // below still uses the _hiZTexture handle for SetTextureBuffer).
    Texture& hiZTex = _hiZTexture.Get();
    Texture& depthTex = depth.Get();

    // Prepare depth as copy source and Hi-Z as copy destination.
    commandBuffer.CreateBarrierBatch()
        .Image(depthTex,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
        .Image(hiZTex,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        .Submit();

    // Copy resolved depth to Hi-Z mip 0
    commandBuffer.CopyImage(depthTex, hiZTex, 0, 0, 0, 0);

    // Restore depth for sampling and move Hi-Z to GENERAL for mip chain generation.
    commandBuffer.CreateBarrierBatch()
        .Image(depthTex,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .Image(hiZTex,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL)
        .Submit();

    // Generate Hi-Z mip chain
    if (_hiZMipLevels > 1)
    {
        commandBuffer.BindPipeline(_hiZPipeline.get());

        uint32_t groupX = (_screenExtent.width + 7) / 8;
        uint32_t groupY = (_screenExtent.height + 7) / 8;

        uint32_t mipWidth = _screenExtent.width;
        uint32_t mipHeight = _screenExtent.height;

        for (uint32_t mip = 1; mip < _hiZMipLevels; ++mip)
        {
            mipWidth = std::max(1u, mipWidth / 2);
            mipHeight = std::max(1u, mipHeight / 2);

            auto& hiZShader = _hiZGenerateShader.Get();
            auto builder = renderFrame.GetResources().CreateDescriptorSetBuilder(hiZShader, 0);
            builder.SetTextureBuffer(0, _hiZTexture, mip - 1, VK_IMAGE_LAYOUT_GENERAL);
            builder.SetTextureBuffer(1, _hiZTexture, mip, VK_IMAGE_LAYOUT_GENERAL);
            auto& resources = builder.Build();

            commandBuffer.BindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, hiZShader,
                resources);

            struct HiZPushConstants {
                int32_t outputWidth;
                int32_t outputHeight;
            } hiZPc = { static_cast<int32_t>(mipWidth), static_cast<int32_t>(mipHeight) };

            commandBuffer.PushConstants(hiZShader, 0, hiZPc);

            groupX = (mipWidth + 7) / 8;
            groupY = (mipHeight + 7) / 8;
            commandBuffer.Dispatch(groupX, groupY, 1);

            commandBuffer.CreateBarrierBatch()
                .Image(hiZTex,
                    VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_GENERAL)
                .Submit();
        }
    }

    // Hi-Z: GENERAL > SHADER_READ_ONLY
    commandBuffer.CreateBarrierBatch()
        .Image(hiZTex,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        .Submit();
}
