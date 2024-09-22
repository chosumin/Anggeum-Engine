#include "stdafx.h"
#include "Utility.h"

VkImageView Core::Utility::CreateImageView(Device& device,
    VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(device.GetDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS)
    {
        throw runtime_error("failed to create texture image view!");
    }

    return imageView;
}

bool Core::Utility::HasStencilComponent(VkFormat format)
{
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

void Core::Utility::CreateImage(Device& device,
    uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples,
    VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, 
    VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory,
    VkImageLayout initialLayout)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.samples = numSamples;

    //VK_IMAGE_LAYOUT_PREINITIALIZED, the first transition will preserve the texels.
    imageInfo.initialLayout = initialLayout;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    //related to sparse images, such as 3D texture for a voxel terrain.
    imageInfo.flags = 0;

    auto handle = device.GetDevice();

    if (vkCreateImage(handle, &imageInfo, nullptr, &image) != VK_SUCCESS)
    {
        throw runtime_error("failed to create image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(handle, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex =
        device.FindMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(handle, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS)
    {
        throw runtime_error("failed to allocate image memory!");
    }

    vkBindImageMemory(handle, image, imageMemory, 0);
}