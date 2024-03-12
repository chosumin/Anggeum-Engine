#include "stdafx.h"
#include "CommandBuffer.h"
#include "Pipeline.h"
#include "SwapChain.h"
#include "Shader.h"
#include "Buffer.h"
#include "Material.h"
#include "CommandPool.h"

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

void Core::CommandBuffer::BeginCommandBuffer()
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(_commandBuffer, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("failed to begin recording command buffer!");
}

void Core::CommandBuffer::BeginRenderPass(VkRenderPassBeginInfo renderPassInfo)
{
    vkCmdBeginRenderPass(_commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void Core::CommandBuffer::BindPipeline(VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline)
{
    vkCmdBindPipeline(_commandBuffer, pipelineBindPoint, pipeline);
}

void Core::CommandBuffer::SetViewportAndScissor(VkViewport viewport, VkRect2D scissor)
{
    vkCmdSetViewport(_commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(_commandBuffer, 0, 1, &scissor);
}

void Core::CommandBuffer::BindDescriptorSets(
    VkPipelineBindPoint pipelineBindPoint, Shader& shader, uint32_t currentFrame)
{
    auto descriptorLayout = shader.GetPipelineLayout();
    auto descriptorSet = shader.GetDescriptorSet(currentFrame);

    vkCmdBindDescriptorSets(
        _commandBuffer, pipelineBindPoint,
        descriptorLayout, 0, 1,
        descriptorSet, 0, nullptr);
}

void Core::CommandBuffer::Flush(Material& material)
{
    auto& shader = material.GetShader();

    auto pushConstants = material.GetPushConstantsData();
    auto shaderStageFlags = shader.GetPushConstantsShaderStage();

    vkCmdPushConstants(_commandBuffer, shader.GetPipelineLayout(), shader.GetPushConstantsShaderStage(), 0, static_cast<uint32_t>(pushConstants->size()), pushConstants->data());

    material.ClearPushConstantsCache();
}

void Core::CommandBuffer::BindVertexBuffers(Buffer& buffer)
{
    VkBuffer vertexBuffers[] = { buffer.GetBuffer() };
    VkDeviceSize offsets[] = { 0 };

    vkCmdBindVertexBuffers(_commandBuffer, 0, 1, vertexBuffers, offsets);
}

void Core::CommandBuffer::BindIndexBuffer(Buffer& buffer, VkIndexType indexType)
{
    vkCmdBindIndexBuffer(_commandBuffer, buffer.GetBuffer(), 0, indexType);
}

void Core::CommandBuffer::DrawIndexed(uint32_t indexCount, uint32_t instanceCount)
{
    vkCmdDrawIndexed(_commandBuffer, indexCount, instanceCount, 0, 0, 0);
}

void Core::CommandBuffer::EndCommandBuffer()
{
    vkCmdEndRenderPass(_commandBuffer);

    if (vkEndCommandBuffer(_commandBuffer) != VK_SUCCESS)
        throw std::runtime_error("failed to record command buffer!");
}