#include "Graphics/Culler.h"
#include "Graphics/RendererBatch.h"
#include "Graphics/Vulkans/Shader.h"
#include "Graphics/Vulkans/Pipeline.h"
#include "Graphics/Vulkans/CommandBuffer.h"
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

    renderFrame.SetShaderStorageBuffer(*_resetDrawCommandsShader, 0, _indirectCommandBuffer);
    renderFrame.SetShaderStorageBuffer(*_resetDrawCommandsShader, 1, _pass2IndirectCommandBuffer);
    renderFrame.SetShaderStorageBuffer(*_resetDrawCommandsShader, 2, _rejectedCountBuffer);

    commandBuffer.PushConstants(*_resetDrawCommandsShader, 0, &drawCount);
    commandBuffer.BindDescriptorSets(renderFrame,
        _resetDrawCommandsPipeline->GetPipelineBindPoint(), *_resetDrawCommandsShader);

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
        _indirectCommandBuffer, _cullingShader, _cullingPipeline.get());
}

void Core::Culler::DispatchPass2Culling(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
    const CameraBuffer& camera, shared_ptr<Texture> depth)
{
    DispatchCulling(renderFrame, commandBuffer, camera, depth,
        _pass2IndirectCommandBuffer, _pass2CullingShader, _pass2CullingPipeline.get());
}

void Core::Culler::DispatchCulling(RenderFrame& renderFrame, CommandBuffer& commandBuffer,
    const CameraBuffer& camera, shared_ptr<Texture> depth,
    Core::Buffer* indirectCommandBuffer,
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

    // Culling dispatch
    GPUCullData cullData{};
    cullData.view = camera.View;
    cullData.proj = camera.Projection;
    cullData.screenSize = glm::vec2(_screenExtent.width, _screenExtent.height);
    cullData.drawCount = _instanceCount;
    cullData.hiZMipLevels = _hiZMipLevels;
    cullData.enableOcclusionCulling = _hiZInitialized ? 1 : 0;

    glm::mat4 viewProj = camera.Projection * camera.View;
    ExtractFrustumPlanes(viewProj, cullData.frustumPlanes);

    commandBuffer.BindPipeline(cullingPipeline);

    renderFrame.SetShaderUniformBuffer(*cullingShader, 0, &cullData);
    renderFrame.SetShaderStorageBuffer(*cullingShader, 1, _objectDataBuffer);
    renderFrame.SetShaderStorageBuffer(*cullingShader, 2, _transformBatch.TransformBuffer);
    renderFrame.SetShaderStorageBuffer(*cullingShader, 3, _instanceBuffer);
    renderFrame.SetShaderStorageBuffer(*cullingShader, 4, indirectCommandBuffer);
    renderFrame.SetShaderTextureBuffer(*cullingShader, 5, _hiZTexture);

    renderFrame.SetShaderStorageBuffer(*cullingShader, 10, _rejectedIndicesBuffer);
    renderFrame.SetShaderStorageBuffer(*cullingShader, 11, _rejectedCountBuffer);

    commandBuffer.BindDescriptorSets(renderFrame,
        cullingPipeline->GetPipelineBindPoint(), *cullingShader);

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
    shared_ptr<Sampler> sampler = device.GetResourceCache().RequestSampler(DEFAULT_SAMPLER);
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

            size_t uniqueKey = (_hiZGenerateShader->GetHash() << 8) | mip;
            auto& resources = renderFrame.GetOrCreateShaderResources(uniqueKey);

            //HACK HACK! Needs a new descriptor set system to avoid this kind of manual setup
            TextureBuffer srcTex{};
            srcTex.texture = _hiZTexture;
            srcTex.mipLevel = mip - 1;
            srcTex.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            resources.textureBuffers[0] = srcTex;

            TextureBuffer dstTex{};
            dstTex.texture = _hiZTexture;
            dstTex.mipLevel = mip;
            dstTex.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            resources.textureBuffers[1] = dstTex;

            renderFrame.AllocateDescriptorSetsWithKey(*_hiZGenerateShader, uniqueKey);
            renderFrame.UpdateDescriptorSetsWithKey(*_hiZGenerateShader, uniqueKey);
            commandBuffer.BindDescriptorSetsWithKey(renderFrame, VK_PIPELINE_BIND_POINT_COMPUTE,
                *_hiZGenerateShader, uniqueKey);

            struct HiZPushConstants {
                int32_t outputWidth;
                int32_t outputHeight;
            } hiZPc = { static_cast<int32_t>(mipWidth), static_cast<int32_t>(mipHeight) };

            commandBuffer.PushConstants(*_hiZGenerateShader, 0, &hiZPc);

            groupX = (mipWidth + 7) / 8;
            groupY = (mipHeight + 7) / 8;
            commandBuffer.Dispatch(groupX, groupY, 1);
        }
    }

    // Hi-Z: GENERAL > SHADER_READ_ONLY
    commandBuffer.TransitionImageLayout(
        hiZTextureImage,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void Core::Culler::DispatchFrustumOnlyCulling(RenderFrame& renderFrame,
    CommandBuffer& commandBuffer, const CameraBuffer& camera)
{
    uint32_t drawCount = _drawCount;

    // Reset instance counts
    commandBuffer.BindPipeline(_resetDrawCommandsSimplePipeline.get());

    renderFrame.SetShaderStorageBuffer(*_resetDrawCommandsSimpleShader, 0, _indirectCommandBuffer);

    commandBuffer.PushConstants(*_resetDrawCommandsSimpleShader, 0, &drawCount);
    commandBuffer.BindDescriptorSets(renderFrame,
        _resetDrawCommandsSimplePipeline->GetPipelineBindPoint(), *_resetDrawCommandsSimpleShader);

    uint32_t groupCount = (drawCount + 63) / 64;
    commandBuffer.Dispatch(std::max(1u, groupCount), 1, 1);

    commandBuffer.Barrier(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

    // Dispatch frustum-only culling
    struct FrustumCullData
    {
        glm::mat4 view;
        glm::mat4 proj;
        glm::vec4 frustumPlanes[6];
        uint32_t drawCount;
    } cullData{};

    cullData.view = camera.View;
    cullData.proj = camera.Projection;
    cullData.drawCount = _instanceCount;

    glm::mat4 viewProj = camera.Projection * camera.View;
    ExtractFrustumPlanes(viewProj, cullData.frustumPlanes);

    commandBuffer.BindPipeline(_frustumCullingPipeline.get());

    renderFrame.SetShaderUniformBuffer(*_frustumCullingShader, 0, &cullData);
    renderFrame.SetShaderStorageBuffer(*_frustumCullingShader, 1, _objectDataBuffer);
    renderFrame.SetShaderStorageBuffer(*_frustumCullingShader, 2, _transformBatch.TransformBuffer);
    renderFrame.SetShaderStorageBuffer(*_frustumCullingShader, 3, _instanceBuffer);
    renderFrame.SetShaderStorageBuffer(*_frustumCullingShader, 4, _indirectCommandBuffer);

    commandBuffer.BindDescriptorSets(renderFrame,
        _frustumCullingPipeline->GetPipelineBindPoint(), *_frustumCullingShader);

    groupCount = (_instanceCount + 63) / 64;
    commandBuffer.Dispatch(groupCount, 1, 1);

    commandBuffer.Barrier(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_READ_BIT);
}
