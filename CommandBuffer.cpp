#include "stdafx.h"
#include "CommandBuffer.h"
#include "Pipeline.h"
#include "SwapChain.h"

Core::CommandBuffer::CommandBuffer()
{
    CreateCommandPool();
    CreateCommandBuffers();
    CreateSyncObjects();
}

Core::CommandBuffer::~CommandBuffer()
{
    auto device = Device::Instance().GetDevice();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        vkDestroySemaphore(device, _imageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(device, _renderFinishedSemaphores[i], nullptr);
        vkDestroyFence(device, _inFlightFences[i], nullptr);
    }

    vkDestroyCommandPool(device, _commandPool, nullptr);
}

void Core::CommandBuffer::RecordCommandBuffer(Pipeline& pipeline, SwapChain& swapChain)
{
    auto device = Device::Instance().GetDevice();

    vkWaitForFences(device, 1, &_inFlightFences[_currentFrame], VK_TRUE, UINT64_MAX);

    VkResult result = vkAcquireNextImageKHR(
        device, swapChain.GetSwapChain(), UINT64_MAX,
        _imageAvailableSemaphores[_currentFrame], VK_NULL_HANDLE, &_imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        swapChain.RecreateSwapChain();
        return;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw runtime_error("failed to acquire swap chain image!");

    vkResetFences(device, 1, &_inFlightFences[_currentFrame]);

    vkResetCommandBuffer(_commandBuffers[_currentFrame], 0);

    RecordCommandBuffer(_commandBuffers[_currentFrame], _imageIndex, pipeline, swapChain);
}

void Core::CommandBuffer::EndFrame(Pipeline& pipeline, SwapChain& swapChain)
{
    EndCommandBuffer(_commandBuffers[_currentFrame]);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = { _imageAvailableSemaphores[_currentFrame]};
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &_commandBuffers[_currentFrame];

    VkSemaphore signalSemaphores[] = { _renderFinishedSemaphores[_currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(Device::Instance().GetGraphicsQueue(), 1, &submitInfo, _inFlightFences[_currentFrame]) != VK_SUCCESS)
        throw runtime_error("failed to submit draw command buffer!");

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = { swapChain.GetSwapChain()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &_imageIndex;

    VkResult result = vkQueuePresentKHR(Device::Instance().GetPresentQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || Window::FramebufferResized)
    {
        Window::FramebufferResized = false;
        swapChain.RecreateSwapChain();
    }
    else if (result != VK_SUCCESS)
        throw runtime_error("failed to present swap chain image!");

    _currentFrame = (_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Core::CommandBuffer::CreateCommandPool()
{
    QueueFamilyIndices queueFamilyIndices = Device::Instance().FindQueueFamilies();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.GraphicsAndComputeFamily.value();

    auto device = Device::Instance().GetDevice();
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &_commandPool) != VK_SUCCESS)
        throw runtime_error("failed to create command pool!");
}

void Core::CommandBuffer::CreateCommandBuffers()
{
    _commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = _commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)_commandBuffers.size();

    if (vkAllocateCommandBuffers(Device::Instance().GetDevice(), &allocInfo, _commandBuffers.data()) != VK_SUCCESS)
        throw runtime_error("failed to allocate command buffers!");
}

void Core::CommandBuffer::RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex,
    Pipeline& pipeline, SwapChain& swapChain)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("failed to begin recording command buffer!");

    VkRenderPassBeginInfo renderPassInfo = swapChain.CreateRenderPassBeginInfo(imageIndex);

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.GetGraphicsPipeline());

    VkViewport viewport{};
    VkRect2D scissor{};
    swapChain.GetViewportAndScissor(viewport, scissor);

    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindDescriptorSets(
        commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline.GetPipelineLayout(), 0, 1,
        pipeline.GetDescriptorSet(_currentFrame), 0, nullptr);
}

void Core::CommandBuffer::EndCommandBuffer(VkCommandBuffer commandBuffer)
{
    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        throw std::runtime_error("failed to record command buffer!");
}

void Core::CommandBuffer::CreateSyncObjects()
{
    _imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    _renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    _inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    auto device = Device::Instance().GetDevice();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &_renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &_inFlightFences[i]) != VK_SUCCESS)
            throw runtime_error("failed to create semaphores!");
    }
}
