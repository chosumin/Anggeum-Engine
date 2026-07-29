#include "Graphics/FrustumCuller.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/ResourceCache.h"

using namespace Core;

Core::FrustumCuller::FrustumCuller(Device& device, RenderFrame& renderFrame,
    RendererBatch& rendererBatch, uint32_t id)
    : Culler(device, rendererBatch, id)
{
    auto& frameResources = renderFrame.GetResources();
    auto& resourceCache = device.GetResourceCache();

    _cullingShader = resourceCache.LoadShader("Shaders/frustumCulling.comp.spv");
    _cullingPipeline = make_unique<Pipeline>(device, _cullingShader.Get());

    _resetDrawCommandsShader = resourceCache.LoadShader("Shaders/resetDrawCommandsSimple.comp.spv");
    _resetDrawCommandsPipeline = make_unique<Pipeline>(device, _resetDrawCommandsShader.Get());

    _cullDataBuffer = frameResources.GetOrCreateUniformBuffer<GPUFrustumCullData>(
        _namePrefix + "FrustumCullData");

    PrepareBatchResources(renderFrame);
}

Shader& Core::FrustumCuller::GetCullingShader() const
{
    return _cullingShader.Get();
}

void Core::FrustumCuller::Dispatch(RenderFrame& renderFrame,
    CommandBuffer& commandBuffer, DescriptorSetBuilder& builder, const CameraBuffer& camera)
{
    uint32_t drawCount = _drawCount;

    // Reset instance counts.
    // Uses its own descriptor set so it doesn't clash with the culling dispatch
    // or with other cullers (e.g. shadow cascades) that share the same shader.
    commandBuffer.BindPipeline(_resetDrawCommandsPipeline.get());

    auto& resetShader = _resetDrawCommandsShader.Get();
    auto resetBuilder = renderFrame.GetResources().CreateDescriptorSetBuilder(resetShader);
    resetBuilder.SetStorageBuffer(0, _indirectCommandBuffer.Get());
    auto& resetResources = resetBuilder.Build();

    commandBuffer.PushConstants(resetShader, 0, drawCount);
    commandBuffer.BindDescriptorSet(_resetDrawCommandsPipeline->GetPipelineBindPoint(),
        resetShader,
        resetResources);

    uint32_t groupCount = (drawCount + 63) / 64;
    commandBuffer.Dispatch(std::max(1u, groupCount), 1, 1);

    commandBuffer.CreateBarrierBatch()
        .Buffer(
            _indirectCommandBuffer.Get(),
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT)
        .Submit();

    // Dispatch frustum-only culling
    GPUFrustumCullData cullData{};
    cullData.view = camera.View;
    cullData.proj = camera.Projection;
    cullData.drawCount = _instanceCount;

    glm::mat4 viewProj = camera.Projection * camera.View;
    ExtractFrustumPlanes(viewProj, cullData.frustumPlanes);

    _cullDataBuffer.Get().Update(cullData);

    commandBuffer.BindPipeline(_cullingPipeline.get());

    // Use the builder so each dispatch gets a fresh descriptor set. Sharing the
    // per-shader-hash cache made multiple cullers (shadow cascades) reuse the
    // first culler's buffers, so their planes/buffers were never bound and
    // nothing got culled.
    builder.SetUniformBuffer(0, _cullDataBuffer.Get());
    builder.SetStorageBuffer(1, _rendererBatch.GetObjectDataBuffer());
    builder.SetStorageBuffer(2, _rendererBatch.GetTransformBatch().TransformBuffer.Get());
    builder.SetStorageBuffer(3, _rendererBatch.GetInstanceBuffer());
    builder.SetStorageBuffer(4, _indirectCommandBuffer.Get());

    auto& resources = builder.Build();

    commandBuffer.BindDescriptorSet(_cullingPipeline->GetPipelineBindPoint(),
        _cullingShader.Get(),
        resources);

    groupCount = (_instanceCount + 63) / 64;
    commandBuffer.Dispatch(groupCount, 1, 1);

    commandBuffer.CreateBarrierBatch()
        .Buffer(
            _indirectCommandBuffer.Get(),
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT)
        .Submit();
}
