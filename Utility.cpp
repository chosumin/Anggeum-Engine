#include "stdafx.h"
#include "Utility.h"

VkCommandBuffer Core::Utility::BeginSingleTimeCommands(VkCommandPool commandPool)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(Device::Instance().GetDevice(), &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void Core::Utility::EndSingleTimeCommands(
    VkCommandPool commandPool, VkCommandBuffer commandBuffer)
{
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    auto graphicsQueue = Device::Instance().GetGraphicsQueue();
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);

    //hack : optimizable with vkWaitForFences.
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(Device::Instance().GetDevice(), 
        commandPool, 1, &commandBuffer);
}

VkImageView Core::Utility::CreateImageView(VkImage image, VkFormat format)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(Device::Instance().GetDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS) 
    {
        throw runtime_error("failed to create texture image view!");
    }

    return imageView;
}
