#include "stdafx.h"
#include "CommandBuffer.h"
#include "Pipeline.h"
#include "SwapChain.h"
#include "Shader.h"
#include "Buffer.h"
#include "Material.h"
#include "CommandPool.h"
#include "Texture.h"
#include "Image.h"

Core::CommandBuffer::CommandBuffer(Device& device, CommandPool& commandPool)
    :_device(device)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool.GetHandle();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(_device.GetDevice(), &allocInfo, &_commandBuffer) != VK_SUCCESS)
        throw runtime_error("failed to allocate command buffers!");
}

void Core::CommandBuffer::ResetCommandBuffer()
{
    vkResetCommandBuffer(_commandBuffer, 0);
}

void Core::CommandBuffer::BeginCommandBuffer(bool isSingleTime)
{
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	if (isSingleTime)
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    auto result = vkBeginCommandBuffer(_commandBuffer, &beginInfo);
    if (result != VK_SUCCESS)
        throw std::runtime_error("failed to begin recording command buffer!");
}

void Core::CommandBuffer::BeginRenderPass(VkRenderPassBeginInfo renderPassInfo)
{
    vkCmdBeginRenderPass(_commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void Core::CommandBuffer::BindPipeline(const Pipeline* pipeline)
{
    vkCmdBindPipeline(_commandBuffer, pipeline->GetPipelineBindPoint(), pipeline->GetGraphicsPipeline());
}

void Core::CommandBuffer::SetViewportAndScissor(VkExtent2D extent)
{
    VkViewport viewport;
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vkCmdSetViewport(_commandBuffer, 0, 1, &viewport);

    VkRect2D scissor;
    scissor.offset = { 0, 0 };
    scissor.extent = extent;

    vkCmdSetScissor(_commandBuffer, 0, 1, &scissor);
}

void Core::CommandBuffer::BindDescriptorSets(
    VkPipelineBindPoint pipelineBindPoint, Material& material, uint32_t currentFrame)
{
	if (material.IsDirty())
		material.UpdateDescriptorSets();

    auto descriptorLayout = material.GetShader().GetPipelineLayout();
    auto descriptorSet = material.GetDescriptorSet(currentFrame);

    vkCmdBindDescriptorSets(
        _commandBuffer, pipelineBindPoint,
        descriptorLayout, 0, 1,
        &descriptorSet, 0, nullptr);
}

void Core::CommandBuffer::PushConstants(Material& material, uint32_t index)
{
    auto& shader = material.GetShader();

    auto pushConstants = material.GetPushConstantsData();

    if (pushConstants->empty())
		return;

	vkCmdPushConstants(_commandBuffer, shader.GetPipelineLayout(),
        shader.GetPushConstantsShaderStage(index),
		shader.GetPushConstantsOffset(index),
		static_cast<uint32_t>(pushConstants->size()),
		pushConstants->data());

	material.ClearPushConstantsCache();
}

void Core::CommandBuffer::BindVertexBuffers(Buffer& buffer, uint32_t binding)
{
    VkBuffer vertexBuffers[] = { buffer.GetBuffer() };
    VkDeviceSize offsets[] = { 0 };

    vkCmdBindVertexBuffers(_commandBuffer, binding, 1, vertexBuffers, offsets);
}

void Core::CommandBuffer::BindVertexBuffers(vector<Buffer*> buffers, uint32_t binding)
{
    vector<VkBuffer> vertexBuffers(buffers.size());
    transform(buffers.begin(), buffers.end(), vertexBuffers.begin(),
        [](Buffer* buf)
    {
        return buf->GetBuffer();
    });

	vector<VkDeviceSize> offsets(buffers.size(), 0);

    vkCmdBindVertexBuffers(_commandBuffer, binding, static_cast<uint32_t>(buffers.size()), vertexBuffers.data(), offsets.data());
}

void Core::CommandBuffer::BindIndexBuffer(Buffer& buffer, VkIndexType indexType)
{
    vkCmdBindIndexBuffer(_commandBuffer, buffer.GetBuffer(), 0, indexType);
}

void Core::CommandBuffer::DrawIndexed(uint32_t indexCount, uint32_t instanceCount)
{
    vkCmdDrawIndexed(_commandBuffer, indexCount, instanceCount, 0, 0, 0);
}

void Core::CommandBuffer::CopyImage(Image& srcImage, Image& dstImage, 
    uint32_t srcMipLevel, uint32_t srcLayer, uint32_t dstMipLevel, uint32_t dstLayer)
{
    VkImageCopy copyRegion{};

    copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.srcSubresource.baseArrayLayer = srcLayer;
    copyRegion.srcSubresource.mipLevel = srcMipLevel;
    copyRegion.srcSubresource.layerCount = 1;
    copyRegion.srcOffset = { 0, 0, 0 };

    copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.dstSubresource.baseArrayLayer = dstLayer;
    copyRegion.dstSubresource.mipLevel = dstMipLevel;
    copyRegion.dstSubresource.layerCount = 1;
    copyRegion.dstOffset = { 0, 0, 0 };

    auto extent = srcImage.GetExtent();

    VkExtent2D mipExtent;
    mipExtent.width = static_cast<uint32_t>(extent.width * pow(0.5f, dstMipLevel));
    mipExtent.height = static_cast<uint32_t>(extent.height * pow(0.5f, dstMipLevel));

    copyRegion.extent.width = static_cast<uint32_t>(mipExtent.width);
    copyRegion.extent.height = static_cast<uint32_t>(mipExtent.height);
    copyRegion.extent.depth = extent.depth;

    vkCmdCopyImage(
        _commandBuffer,
        srcImage.GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dstImage.GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copyRegion);
}

void Core::CommandBuffer::TransitionImageLayout(Image& image, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    //모든 밉맵 이미지에 같은 레이아웃을 적용.
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.GetImage();
    barrier.subresourceRange.aspectMask = image.GetAspectFlags();
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = image.GetMipLevel();
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = image.GetLayer();

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    //tranfer writes that don't need to wait on anything.
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
        newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
        newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
        newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }
    else
    {
        throw invalid_argument("unsupported layout transition!");
    }

    vkCmdPipelineBarrier(
        _commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

void Core::CommandBuffer::EndRenderPass()
{
    vkCmdEndRenderPass(_commandBuffer);
}

void Core::CommandBuffer::EndCommandBuffer()
{
    if (vkEndCommandBuffer(_commandBuffer) != VK_SUCCESS)
        throw std::runtime_error("failed to record command buffer!");
}