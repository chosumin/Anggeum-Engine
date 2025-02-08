#include "stdafx.h"
#include "Image.h"
#include "Buffer.h"
#include "CommandBuffer.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

Core::Image::Image(Device& device, vector<uint8_t>&& data)
    :_device(device)
{
    int width, height, comp;
    int reqComp = 4;

    const uint8_t* buffer = reinterpret_cast<const stbi_uc*>(data.data());
    auto bufferSize = static_cast<int>(data.size());

    auto pixels = stbi_load_from_memory(buffer, bufferSize, &width, &height, &comp, reqComp);

    if (pixels == nullptr)
    {
        auto reason = stbi_failure_reason();
        throw std::runtime_error{ reason };
    }

    VkExtent3D extent;
    extent.depth = 1u;
    extent.width = static_cast<uint32_t>(width);
    extent.height = static_cast<uint32_t>(height);

    CreateImage(pixels, extent);
}

Core::Image::Image(Device& device, string filePath)
    :_device(device)
{
    int width, height, comp;
    int reqComp = 4;

    stbi_uc* pixels = stbi_load(filePath.c_str(),
        &width, &height, &comp, static_cast<int>(reqComp));

    if (pixels == nullptr)
        throw runtime_error("failed to load texture image!");

    VkExtent3D extent;
    extent.depth = 1u;
    extent.width = static_cast<uint32_t>(width);
    extent.height = static_cast<uint32_t>(height);

    CreateImage(pixels, extent);
}

Core::Image::~Image()
{
    auto device = _device.GetDevice();

    vkDestroyImageView(device, _imageView, nullptr);
    vkFreeMemory(device, _imageMemory, nullptr);
    vkDestroyImage(device, _image, nullptr);
}

void Core::Image::CreateImage(void* pixels, VkExtent3D extent)
{
    int mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(extent.width, extent.height)))) + 1;

    CreateImage(extent, mipLevels, 
        VK_SAMPLE_COUNT_1_BIT,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_TILING_OPTIMAL, //VK_IMAGE_TILING_LINEAR to directly access texels in the memory.
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED);

    BindImageMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkDeviceSize imageSize = extent.width * extent.height * 4;

    Buffer stagingBuffer(_device, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    stagingBuffer.CopyBuffer(pixels, imageSize);

    stbi_image_free(pixels);

    TransitionImageLayout(_image,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);

    CopyBufferToImage(stagingBuffer.GetBuffer(),
        _image, extent.width, extent.height);

    CreateImageView(VK_FORMAT_R8G8B8A8_SRGB, mipLevels);
}

void Core::Image::CreateImage(VkExtent3D extent, uint32_t mipLevels,
    VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling,
    VkImageUsageFlags usage, VkImageLayout initialLayout)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = extent;
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

    if (vkCreateImage(_device.GetDevice(), &imageInfo, nullptr, &_image) != VK_SUCCESS)
    {
        throw runtime_error("failed to create image!");
    }
}

void Core::Image::BindImageMemory(VkMemoryPropertyFlags properties)
{
    auto device = _device.GetDevice();

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, _image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex =
        _device.FindMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &_imageMemory) != VK_SUCCESS)
    {
        throw runtime_error("failed to allocate image memory!");
    }

    vkBindImageMemory(device, _image, _imageMemory, 0);
}

void Core::Image::CreateImageView(VkFormat format, uint32_t mipLevels)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = _image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(_device.GetDevice(), &viewInfo, nullptr, &_imageView) != VK_SUCCESS)
    {
        throw runtime_error("failed to create texture image view!");
    }
}

void Core::Image::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
{
    auto& commandBuffer = _device.BeginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, 1 };

    //dstImageLayout >> 현재 사용 중인 레이아웃, 이 명령 이전에 이 레이아웃으로 트랜지션 되어야 함.
    vkCmdCopyBufferToImage(
        commandBuffer.GetHandle(),
        buffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    _device.EndSingleTimeCommands(commandBuffer);
}

void Core::Image::TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels)
{
    auto& commandBuffer = _device.BeginSingleTimeCommands();

    //모든 밉맵 이미지에 같은 레이아웃을 적용.
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

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
    else
    {
        throw invalid_argument("unsupported layout transition!");
    }

    vkCmdPipelineBarrier(
        commandBuffer.GetHandle(),
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    _device.EndSingleTimeCommands(commandBuffer);
}
