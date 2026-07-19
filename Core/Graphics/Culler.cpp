#include "Graphics/Culler.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/DescriptorSetBuilder.h"
#include "Graphics/RenderFrame.h"
#include "Graphics/ResourceCache.h"
#include "Vulkans/Texture.h"
#include "TransferJob.h"

using namespace Core;

Core::Culler::Culler(Device& device, TransformBatch& transformBatch)
    : _device(device)
    , _transformBatch(transformBatch)
{
}

Core::Culler::~Culler()
{
    if (_rejectedIndicesBuffer != nullptr) delete(_rejectedIndicesBuffer);
    if (_rejectedCountBuffer != nullptr) delete(_rejectedCountBuffer);
    if (_indirectCommandBuffer != nullptr) delete(_indirectCommandBuffer);
    if (_pass2IndirectCommandBuffer != nullptr) delete(_pass2IndirectCommandBuffer);
}

void Core::Culler::Prepare(Device& device, VkExtent2D extents,
    Buffer* objectDataBuffer, Buffer* instanceBuffer,
    uint32_t instanceCount,
    const IndirectDrawBuffer& indirectDrawBuffer)
{
    _objectDataBuffer = objectDataBuffer;
    _instanceBuffer = instanceBuffer;
    _instanceCount = instanceCount;
    _drawCount = indirectDrawBuffer.GetDrawCount();

    PrepareHiZResources(device, extents);
    PrepareCullingResources(device, indirectDrawBuffer);
}

void Core::Culler::PrepareCullingResources(Core::Device& device, const IndirectDrawBuffer& indirectDrawBuffer)
{
    _cullingShader = device.GetResourceCache().RequestShader("Shaders/gpuCulling.comp.spv");
    _cullingPipeline = make_unique<Pipeline>(device, *_cullingShader);

    _pass1CullDataBuffer = make_unique<Core::Buffer>(device, sizeof(GPUCullData),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, MemoryType::UNIFORM);
    _pass2CullDataBuffer = make_unique<Core::Buffer>(device, sizeof(GPUCullData),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, MemoryType::UNIFORM);
    _frustumCullDataBuffer = make_unique<Core::Buffer>(device, sizeof(GPUFrustumCullData),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, MemoryType::UNIFORM);

    _rejectedIndicesBuffer = new Core::Buffer(device,
        _instanceCount * sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        MemoryType::DEVICE_LOCAL);

    _rejectedCountBuffer = new Core::Buffer(device,
        sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        MemoryType::DEVICE_LOCAL);

    // Pass 1 Indirect Command Buffer (owned by this Culler)
    Core::VkBufferJob<DrawIndexedIndirectCommand> pass1Job(device,
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        &_indirectCommandBuffer,
        indirectDrawBuffer.GetDrawCommands(), 0);
    Core::CommandBuffer::ImmediateSubmit(device, pass1Job);

    // Pass 2 Indirect Command Buffer
    Core::VkBufferJob<DrawIndexedIndirectCommand> pass2Job(device,
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        &_pass2IndirectCommandBuffer,
        indirectDrawBuffer.GetDrawCommands(), 0);
    Core::CommandBuffer::ImmediateSubmit(device, pass2Job);

    _pass2CullingShader = device.GetResourceCache().RequestShader("Shaders/gpuCullingPass2.comp.spv");
    _pass2CullingPipeline = make_unique<Pipeline>(device, *_pass2CullingShader);

    // Reset draw commands shader (2-pass)
    _resetDrawCommandsShader = device.GetResourceCache().RequestShader("Shaders/resetDrawCommands.comp.spv");
    _resetDrawCommandsPipeline = make_unique<Pipeline>(device, *_resetDrawCommandsShader);

    // Frustum-only culling resources
    _frustumCullingShader = device.GetResourceCache().RequestShader("Shaders/frustumCulling.comp.spv");
    _frustumCullingPipeline = make_unique<Pipeline>(device, *_frustumCullingShader);

    _resetDrawCommandsSimpleShader = device.GetResourceCache().RequestShader("Shaders/resetDrawCommandsSimple.comp.spv");
    _resetDrawCommandsSimplePipeline = make_unique<Pipeline>(device, *_resetDrawCommandsSimpleShader);
}

void Core::Culler::ResetDrawCommands(RenderFrame& renderFrame, CommandBuffer& commandBuffer)
{
    uint32_t drawCount = _drawCount;

    commandBuffer.BindPipeline(_resetDrawCommandsPipeline.get());

    auto builder = renderFrame.CreateDescriptorSetBuilder(*_resetDrawCommandsShader, 0);
    builder.SetStorageBuffer(0, _indirectCommandBuffer);
    builder.SetStorageBuffer(1, _pass2IndirectCommandBuffer);
    builder.SetStorageBuffer(2, _rejectedCountBuffer);
    auto& resources = builder.Build();

    commandBuffer.PushConstants(*_resetDrawCommandsShader, 0, drawCount);
    commandBuffer.BindDescriptorSet(_resetDrawCommandsPipeline->GetPipelineBindPoint(),
        *_resetDrawCommandsShader, resources);

    uint32_t groupCount = (drawCount + 63) / 64;
    commandBuffer.Dispatch(std::max(1u, groupCount), 1, 1);

    commandBuffer.Barrier(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
}

void Core::Culler::DispatchPass1Culling(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
    const CameraBuffer& camera, shared_ptr<Texture> depth)
{
    DispatchCulling(renderFrame, commandBuffer, camera, depth,
        _indirectCommandBuffer, *_pass1CullDataBuffer,
        _cullingShader, _cullingPipeline.get());
}

void Core::Culler::DispatchPass2Culling(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
    const CameraBuffer& camera, shared_ptr<Texture> depth)
{
    DispatchCulling(renderFrame, commandBuffer, camera, depth,
        _pass2IndirectCommandBuffer, *_pass2CullDataBuffer,
        _pass2CullingShader, _pass2CullingPipeline.get());
}

void Core::Culler::DispatchCulling(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
    const CameraBuffer& camera, shared_ptr<Texture> depth,
    Core::Buffer* indirectCommandBuffer, Core::Buffer& cullDataBuffer,
    shared_ptr<Shader> cullingShader, Pipeline* cullingPipeline)
{
    // Generate Hi-Z from depth
    if (!_hiZInitialized)
    {
        auto& hiZImage = *_hiZTexture->GetImage().lock();
        commandBuffer.TransitionImageLayout(hiZImage,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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

    auto builder = renderFrame.CreateDescriptorSetBuilder(*cullingShader, 0);
    builder.SetUniformBuffer(0, cullDataBuffer);
    builder.SetStorageBuffer(1, _objectDataBuffer);
    builder.SetStorageBuffer(2, _transformBatch.TransformBuffer);
    builder.SetStorageBuffer(3, _instanceBuffer);
    builder.SetStorageBuffer(4, indirectCommandBuffer);
    builder.SetTextureBuffer(5, _hiZTexture);
    builder.SetStorageBuffer(10, _rejectedIndicesBuffer);
    builder.SetStorageBuffer(11, _rejectedCountBuffer);
    auto& resources = builder.Build();

    commandBuffer.BindDescriptorSet(cullingPipeline->GetPipelineBindPoint(),
        *cullingShader, resources);

    uint32_t groupCount = (_instanceCount + 63) / 64;
    commandBuffer.Dispatch(groupCount, 1, 1);

    commandBuffer.Barrier(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT);
}

void Core::Culler::ExtractFrustumPlanes(const glm::mat4& viewProj, glm::vec4* planes)
{
    // Left
    planes[0] = glm::vec4(
        viewProj[0][3] + viewProj[0][0],
        viewProj[1][3] + viewProj[1][0],
        viewProj[2][3] + viewProj[2][0],
        viewProj[3][3] + viewProj[3][0]);

    // Right
    planes[1] = glm::vec4(
        viewProj[0][3] - viewProj[0][0],
        viewProj[1][3] - viewProj[1][0],
        viewProj[2][3] - viewProj[2][0],
        viewProj[3][3] - viewProj[3][0]);

    // Bottom
    planes[2] = glm::vec4(
        viewProj[0][3] + viewProj[0][1],
        viewProj[1][3] + viewProj[1][1],
        viewProj[2][3] + viewProj[2][1],
        viewProj[3][3] + viewProj[3][1]);

    // Top
    planes[3] = glm::vec4(
        viewProj[0][3] - viewProj[0][1],
        viewProj[1][3] - viewProj[1][1],
        viewProj[2][3] - viewProj[2][1],
        viewProj[3][3] - viewProj[3][1]);

    // Near
    planes[4] = glm::vec4(
        viewProj[0][3] + viewProj[0][2],
        viewProj[1][3] + viewProj[1][2],
        viewProj[2][3] + viewProj[2][2],
        viewProj[3][3] + viewProj[3][2]);

    // Far
    planes[5] = glm::vec4(
        viewProj[0][3] - viewProj[0][2],
        viewProj[1][3] - viewProj[1][2],
        viewProj[2][3] - viewProj[2][2],
        viewProj[3][3] - viewProj[3][2]);

    // Normalize planes
    for (int i = 0; i < 6; ++i)
    {
        float length = glm::length(glm::vec3(planes[i]));
        planes[i] /= length;
    }
}

void Core::Culler::PrepareHiZResources(Device& device, VkExtent2D extents)
{
    _screenExtent = extents;

    // Calculate mip levels
    uint32_t maxDim = std::max(_screenExtent.width, _screenExtent.height);
    _hiZMipLevels = static_cast<uint32_t>(std::floor(std::log2(maxDim))) + 1;

    // Create Hi-Z texture with mip chain (1x sample)
    VkImageCreateInfo imageCreateInfo{};
    imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = VK_FORMAT_R32_SFLOAT;
    imageCreateInfo.extent.width = _screenExtent.width;
    imageCreateInfo.extent.height = _screenExtent.height;
    imageCreateInfo.extent.depth = 1;
    imageCreateInfo.mipLevels = _hiZMipLevels;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    shared_ptr<Image> image = make_shared<Image>(_device, imageCreateInfo, VK_IMAGE_ASPECT_COLOR_BIT);

    auto samplerDesc = DEFAULT_SAMPLER;
    samplerDesc.minFilter = VK_FILTER_NEAREST;
    samplerDesc.magFilter = VK_FILTER_NEAREST;
    samplerDesc.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    shared_ptr<Sampler> sampler = device.GetResourceCache().RequestSampler(samplerDesc);
    _hiZTexture = make_shared<Texture>("HiZ", image, sampler);

    // Load shaders
    _hiZGenerateShader = device.GetResourceCache().RequestShader("Shaders/hiZGenerate.comp.spv");
    _hiZPipeline = make_unique<Pipeline>(device, *_hiZGenerateShader);
}

void Core::Culler::GenerateHiZBuffer(RenderFrame& renderFrame, CommandBuffer& commandBuffer, shared_ptr<Texture> depth)
{
    auto& hiZTextureImage = *_hiZTexture->GetImage().lock();
    auto& depthBufferImage = *depth->GetImage().lock();

    // Transition resolved depth: SHADER_READ_ONLY > TRANSFER_SRC
    commandBuffer.TransitionImageLayout(depthBufferImage,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    // Hi-Z texture: UNDEFINED > TRANSFER_DST
    commandBuffer.TransitionImageLayout(hiZTextureImage,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy resolved depth to Hi-Z mip 0
    commandBuffer.CopyImage(depthBufferImage, hiZTextureImage, 0, 0, 0, 0);

    // Transition resolved depth back: TRANSFER_SRC > SHADER_READ_ONLY
    commandBuffer.TransitionImageLayout(depthBufferImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Hi-Z texture: TRANSFER_DST > GENERAL (for mip chain generation)
    commandBuffer.TransitionImageLayout(hiZTextureImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_GENERAL);

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

            auto builder = renderFrame.CreateDescriptorSetBuilder(*_hiZGenerateShader, 0);
            builder.SetTextureBuffer(0, _hiZTexture, mip - 1, VK_IMAGE_LAYOUT_GENERAL);
            builder.SetTextureBuffer(1, _hiZTexture, mip, VK_IMAGE_LAYOUT_GENERAL);
            auto& resources = builder.Build();

            commandBuffer.BindDescriptorSet(VK_PIPELINE_BIND_POINT_COMPUTE, *_hiZGenerateShader,
                resources);

            struct HiZPushConstants {
                int32_t outputWidth;
                int32_t outputHeight;
            } hiZPc = { static_cast<int32_t>(mipWidth), static_cast<int32_t>(mipHeight) };

            commandBuffer.PushConstants(*_hiZGenerateShader, 0, hiZPc);

            groupX = (mipWidth + 7) / 8;
            groupY = (mipHeight + 7) / 8;
            commandBuffer.Dispatch(groupX, groupY, 1);

            commandBuffer.TransitionImageLayout(
                hiZTextureImage,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_GENERAL);
        }
    }

    // Hi-Z: GENERAL > SHADER_READ_ONLY
    commandBuffer.TransitionImageLayout(
        hiZTextureImage,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void Core::Culler::DispatchFrustumOnlyCulling(RenderFrame& renderFrame,
	CommandBuffer& commandBuffer, DescriptorSetBuilder& builder, const CameraBuffer& camera)
{
	uint32_t drawCount = _drawCount;

	// Reset instance counts.
	// Uses its own descriptor set so it doesn't clash with the culling dispatch
	// or with other cullers (e.g. shadow cascades) that share the same shader.
	commandBuffer.BindPipeline(_resetDrawCommandsSimplePipeline.get());

	auto resetBuilder = renderFrame.CreateDescriptorSetBuilder(*_resetDrawCommandsSimpleShader);
	resetBuilder.SetStorageBuffer(0, _indirectCommandBuffer);
	auto& resetResources = resetBuilder.Build();

	commandBuffer.PushConstants(*_resetDrawCommandsSimpleShader, 0, drawCount);
	commandBuffer.BindDescriptorSet(_resetDrawCommandsSimplePipeline->GetPipelineBindPoint(),
		*_resetDrawCommandsSimpleShader,
		resetResources);

	uint32_t groupCount = (drawCount + 63) / 64;
	commandBuffer.Dispatch(std::max(1u, groupCount), 1, 1);

	commandBuffer.BufferBarrier(
		*_indirectCommandBuffer,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_ACCESS_SHADER_WRITE_BIT,
		VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

	// Dispatch frustum-only culling
	GPUFrustumCullData cullData{};
	cullData.view = camera.View;
	cullData.proj = camera.Projection;
	cullData.drawCount = _instanceCount;

	glm::mat4 viewProj = camera.Projection * camera.View;
	ExtractFrustumPlanes(viewProj, cullData.frustumPlanes);

	_frustumCullDataBuffer->Update(cullData);

	commandBuffer.BindPipeline(_frustumCullingPipeline.get());

	// Use the builder so each dispatch gets a fresh descriptor set. Sharing the
	// per-shader-hash cache made multiple cullers (shadow cascades) reuse the
	// first culler's buffers, so their planes/buffers were never bound and
	// nothing got culled.
	builder.SetUniformBuffer(0, *_frustumCullDataBuffer);
	builder.SetStorageBuffer(1, _objectDataBuffer);
	builder.SetStorageBuffer(2, _transformBatch.TransformBuffer);
	builder.SetStorageBuffer(3, _instanceBuffer);
	builder.SetStorageBuffer(4, _indirectCommandBuffer);

	auto& resources = builder.Build();

	commandBuffer.BindDescriptorSet(_frustumCullingPipeline->GetPipelineBindPoint(),
		*_frustumCullingShader,
		resources);

	groupCount = (_instanceCount + 63) / 64;
	commandBuffer.Dispatch(groupCount, 1, 1);

	commandBuffer.BufferBarrier(
		*_indirectCommandBuffer,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
		VK_ACCESS_SHADER_WRITE_BIT,
		VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT);
}
