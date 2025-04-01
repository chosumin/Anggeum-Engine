#include "stdafx.h"
#include "Image.h"
#include "Buffer.h"
#include "CommandBuffer.h"
#include "Utils/FileSystem.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <ktx.h>
#include <ktxvulkan.h>

Core::Image::Image(Device& device, string filePath, 
    VkImageViewType imageViewType, VkImageCreateFlags flags)
    :_device(device)
{
    _format = VK_FORMAT_R8G8B8A8_SRGB;

    LoadRawImage(filePath);
    CreateImage(_extent, imageViewType, flags);
}

Core::Image::~Image()
{
    auto device = _device.GetDevice();

    vkDestroyImageView(device, _imageView, nullptr);
    vkFreeMemory(device, _imageMemory, nullptr);
    vkDestroyImage(device, _image, nullptr);
}

void Core::Image::SetSRGBFormat()
{
    VkFormat srgb = _format;
    switch (_format)
    {
    case VK_FORMAT_R8_UNORM:
        srgb = VK_FORMAT_R8_SRGB; break;
    case VK_FORMAT_R8G8_UNORM:
        srgb = VK_FORMAT_R8G8_SRGB; break;
    case VK_FORMAT_R8G8B8_UNORM:
        srgb = VK_FORMAT_R8G8B8_SRGB; break;
    case VK_FORMAT_B8G8R8_UNORM:
        srgb = VK_FORMAT_B8G8R8_SRGB; break;
    case VK_FORMAT_R8G8B8A8_UNORM:
        srgb = VK_FORMAT_R8G8B8A8_SRGB; break;
    case VK_FORMAT_B8G8R8A8_UNORM:
        srgb = VK_FORMAT_B8G8R8A8_SRGB; break;
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
        srgb = VK_FORMAT_A8B8G8R8_SRGB_PACK32; break;
    case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        srgb = VK_FORMAT_BC1_RGB_SRGB_BLOCK; break;
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        srgb = VK_FORMAT_BC1_RGBA_SRGB_BLOCK; break;
    case VK_FORMAT_BC2_UNORM_BLOCK:
        srgb = VK_FORMAT_BC2_SRGB_BLOCK; break;
    case VK_FORMAT_BC3_UNORM_BLOCK:
        srgb = VK_FORMAT_BC3_SRGB_BLOCK; break;
    case VK_FORMAT_BC7_UNORM_BLOCK:
        srgb = VK_FORMAT_BC7_SRGB_BLOCK; break;
    case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
        srgb = VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK; break;
    case VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK:
        srgb = VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK; break;
    case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
        srgb = VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK; break;
    case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
        srgb = VK_FORMAT_ASTC_4x4_SRGB_BLOCK; break;
    case VK_FORMAT_ASTC_5x4_UNORM_BLOCK:
        srgb = VK_FORMAT_ASTC_5x4_SRGB_BLOCK; break;
    case VK_FORMAT_ASTC_5x5_UNORM_BLOCK:
        srgb = VK_FORMAT_ASTC_5x5_SRGB_BLOCK; break;
    case VK_FORMAT_ASTC_6x5_UNORM_BLOCK:
        srgb = VK_FORMAT_ASTC_6x5_SRGB_BLOCK; break;
    case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
        srgb = VK_FORMAT_ASTC_6x6_SRGB_BLOCK; break;
    case VK_FORMAT_ASTC_8x5_UNORM_BLOCK:
        srgb = VK_FORMAT_ASTC_8x5_SRGB_BLOCK; break;
    case VK_FORMAT_ASTC_8x6_UNORM_BLOCK:
        srgb = VK_FORMAT_ASTC_8x6_SRGB_BLOCK; break;
    case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:
        srgb = VK_FORMAT_ASTC_8x8_SRGB_BLOCK; break;
    case VK_FORMAT_ASTC_10x5_UNORM_BLOCK:
        srgb = VK_FORMAT_ASTC_10x5_SRGB_BLOCK; break;
    case VK_FORMAT_ASTC_10x6_UNORM_BLOCK:
        srgb = VK_FORMAT_ASTC_10x6_SRGB_BLOCK; break;
    case VK_FORMAT_ASTC_10x8_UNORM_BLOCK:
        srgb = VK_FORMAT_ASTC_10x8_SRGB_BLOCK; break;
    case VK_FORMAT_ASTC_10x10_UNORM_BLOCK:
        srgb = VK_FORMAT_ASTC_10x10_SRGB_BLOCK; break;
    case VK_FORMAT_ASTC_12x10_UNORM_BLOCK:
        srgb = VK_FORMAT_ASTC_12x10_SRGB_BLOCK; break;
    case VK_FORMAT_ASTC_12x12_UNORM_BLOCK:
        srgb = VK_FORMAT_ASTC_12x12_SRGB_BLOCK; break;
    case VK_FORMAT_PVRTC1_2BPP_UNORM_BLOCK_IMG:
        srgb = VK_FORMAT_PVRTC1_2BPP_SRGB_BLOCK_IMG; break;
    case VK_FORMAT_PVRTC1_4BPP_UNORM_BLOCK_IMG:
        srgb = VK_FORMAT_PVRTC1_4BPP_SRGB_BLOCK_IMG; break;
    case VK_FORMAT_PVRTC2_2BPP_UNORM_BLOCK_IMG:
        srgb = VK_FORMAT_PVRTC2_2BPP_SRGB_BLOCK_IMG; break;
    case VK_FORMAT_PVRTC2_4BPP_UNORM_BLOCK_IMG:
        srgb = VK_FORMAT_PVRTC2_4BPP_SRGB_BLOCK_IMG; break;
    }

    _format = srgb;
}

void Core::Image::LoadRawImage(const string& filePath)
{
    string extension = FileSystem::GetExtension(filePath);

    if (extension == "ktx")
    {
        LoadKtxImage(filePath);
    }
    else if (extension == "png" || extension == "jpg")
    {
        LoadStbImage(filePath);
    }
}

void Core::Image::LoadStbImage(const string& filePath)
{
    int width, height, comp;
    int reqComp = 4;

    stbi_uc* pixels = stbi_load(filePath.c_str(),
        &width, &height, &comp, static_cast<int>(reqComp));

    if (pixels == nullptr)
        throw runtime_error("failed to load texture image!");

    _data = { pixels, pixels + (width * height * reqComp) };

    _extent.depth = 1u;
    _extent.width = static_cast<uint32_t>(width);
    _extent.height = static_cast<uint32_t>(height);

    _layer = 1;

    stbi_image_free(pixels);
}

void Core::Image::LoadKtxImage(const string& path)
{
    auto data = FileSystem::Read(path);

    auto dataBuffer = reinterpret_cast<const ktx_uint8_t*>(data.data());
    auto dataSize = static_cast<ktx_size_t>(data.size());

    ktxTexture* texture;
    auto ktxResult = ktxTexture_CreateFromMemory(
        dataBuffer, dataSize, 
        KTX_TEXTURE_CREATE_NO_FLAGS, &texture);

    if (ktxResult != KTX_SUCCESS)
    {
        throw runtime_error{ "Error loading KTX texture: " + path };
    }

    if (texture->pData)
    {
        _data = { texture->pData , texture->pData + texture->dataSize };
    }
    else
    {
        ktx_size_t dataSize = texture->dataSize;
		_data.resize(dataSize);
        auto loadDataResult = ktxTexture_LoadImageData(texture, _data.data(), dataSize);

        if (loadDataResult != KTX_SUCCESS)
        {
			throw runtime_error{ "Error loading KTX texture: " + path };
        }
    }

    _extent.depth = texture->baseDepth;
    _extent.width = texture->baseWidth;
    _extent.height = texture->baseHeight;

    _layer = texture->numLayers;

    // Use the faces if there are 6 (for cubemap)
    if (texture->numLayers == 1 && texture->numFaces == 6)
    {
        _layer = texture->numFaces;
    }

    ktxTexture_Destroy(texture);
}

void Core::Image::CreateImage(VkExtent3D extent, 
    VkImageViewType imageViewType, VkImageCreateFlags flags)
{
    int mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(extent.width, extent.height)))) + 1;

    CreateImage(extent, mipLevels, 
        VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_TILING_OPTIMAL, //VK_IMAGE_TILING_LINEAR to directly access texels in the memory.
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        flags);

    BindImageMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    //VkDeviceSize imageSize = extent.width * extent.height * 4;
    VkDeviceSize imageSize = _data.size();

    Buffer stagingBuffer(_device, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    stagingBuffer.CopyBuffer(_data.data(), imageSize);

    TransitionImageLayout(_image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);

    CopyBufferToImage(stagingBuffer.GetBuffer(),
        _image, extent.width, extent.height);

    //hack : need to be pregenerated and stored in the texture file to improve loading speed.
    GenerateMipmaps(mipLevels);

    CreateImageView(mipLevels, imageViewType);
}

void Core::Image::CreateImage(VkExtent3D extent, uint32_t mipLevels,
    VkSampleCountFlagBits numSamples, VkImageTiling tiling,
    VkImageUsageFlags usage, VkImageLayout initialLayout, VkImageCreateFlags flags)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = extent;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = _layer;
    imageInfo.format = _format;
    imageInfo.tiling = tiling;
    imageInfo.samples = numSamples;

    //VK_IMAGE_LAYOUT_PREINITIALIZED, the first transition will preserve the texels.
    imageInfo.initialLayout = initialLayout;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    //related to sparse images, such as 3D texture for a voxel terrain.
    imageInfo.flags = flags;

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

void Core::Image::CreateImageView(uint32_t mipLevels, VkImageViewType imageViewType)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = _image;
    viewInfo.viewType = imageViewType;
    viewInfo.format = _format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = _layer;

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
    region.imageSubresource.layerCount = _layer;

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

void Core::Image::TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels)
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
    barrier.subresourceRange.layerCount = _layer;

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

void Core::Image::GenerateMipmaps(uint32_t mipLevels)
{
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(
        _device.GetPhysicalDevice(), _format, &formatProperties);

    if (!(formatProperties.optimalTilingFeatures &
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
        throw runtime_error("texture image format doesn't support linear blitting!");

    auto& commandBuffer = _device.BeginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = _image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = _layer;
    barrier.subresourceRange.levelCount = 1; //하나의 밉맵만 레이아웃 변경.

    int32_t mipWidth = _extent.width;
    int32_t mipHeight = _extent.height;

    for (uint32_t i = 1; i < mipLevels; ++i)
    {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        //DST에서 SRC로 레이아웃 변경.
        //Tranfer를 기다린 후 Tranfer에서 실행 >> 이전 Tranfer 스테이지의 커맨드를 모두 수행한 후 이 루프를 실행 함. 
        vkCmdPipelineBarrier(commandBuffer.GetHandle(),
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        VkImageBlit blit{};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = _layer;
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = _layer;

        //both the src and dst are the same image, because of blitting between different levels of the same image.
        vkCmdBlitImage(commandBuffer.GetHandle(),
            _image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            _image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);

        //i - 1을 쉐이더용 레이아웃으로 변경.
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        //This transition waits on the current blit command to finish.
        vkCmdPipelineBarrier(commandBuffer.GetHandle(),
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        if (mipWidth > 1)
            mipWidth /= 2;
        if (mipHeight > 1)
            mipHeight /= 2;
    }

    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer.GetHandle(),
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        0, nullptr,
        0, nullptr,
        1, &barrier);

    _device.EndSingleTimeCommands(commandBuffer);
}
