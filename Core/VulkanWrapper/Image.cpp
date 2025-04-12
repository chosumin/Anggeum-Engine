#include "stdafx.h"
#include "Image.h"
#include "Buffer.h"
#include "CommandBuffer.h"
#include "Utils/FileSystem.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <ktx.h>
#include <ktxvulkan.h>

Core::Image::Image(Device& device, string filePath, VkSampleCountFlagBits sampleCount,
    VkImageViewType imageViewType, VkImageCreateFlags flags)
    :_device(device), _sampleCount(sampleCount)
{
    _format = VK_FORMAT_R8G8B8A8_UNORM;
    _usageFlags = 
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | 
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | 
        VK_IMAGE_USAGE_SAMPLED_BIT;

    LoadRawImage(filePath);

    _mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(_extent.width, _extent.height)))) + 1;

    CreateImage(
        VK_IMAGE_TILING_OPTIMAL, //VK_IMAGE_TILING_LINEAR to directly access texels in the memory.
        _usageFlags,
        VK_IMAGE_LAYOUT_UNDEFINED,
        flags);

    BindImageMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkDeviceSize imageSize = _data.size();

    Buffer stagingBuffer(_device, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    stagingBuffer.CopyBuffer(_data.data(), imageSize);

    auto& commandBuffer = _device.BeginSingleTimeCommands();
    
    commandBuffer.TransitionImageLayout(*this, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    commandBuffer.CopyBufferToImage(stagingBuffer, *this, _extent.width, _extent.height);

    //hack : need to be pregenerated and stored in the texture file to improve loading speed.
    if (_mipLevels > 1)
        commandBuffer.GenerateMipmaps(*this, _mipLevels);

    _device.EndSingleTimeCommands(commandBuffer);

    CreateImageView(_mipLevels, imageViewType, VK_IMAGE_ASPECT_COLOR_BIT);
}

Core::Image::Image(Device& device, VkImageCreateInfo& imageInfo, 
    VkImageAspectFlags aspectFlags, VkImageViewType imageViewType)
	:_device(device), _format(imageInfo.format), _extent(imageInfo.extent), _sampleCount(imageInfo.samples), _mipLevels(imageInfo.mipLevels), _usageFlags(imageInfo.usage),
    _layer(imageInfo.arrayLayers)
{
	CreateImage(
		VK_IMAGE_TILING_OPTIMAL,
		_usageFlags,
		VK_IMAGE_LAYOUT_UNDEFINED,
        imageInfo.flags);

	BindImageMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	CreateImageView(_mipLevels, imageViewType, aspectFlags);
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

VkImageAspectFlags Core::Image::GetAspectFlags() const
{
    if (_format == VK_FORMAT_D16_UNORM ||
        _format == VK_FORMAT_D32_SFLOAT)
        return VK_IMAGE_ASPECT_DEPTH_BIT;

    if (_format == VK_FORMAT_D16_UNORM_S8_UINT ||
        _format == VK_FORMAT_D24_UNORM_S8_UINT ||
        _format == VK_FORMAT_D32_SFLOAT_S8_UINT)
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

    return VK_IMAGE_ASPECT_COLOR_BIT;
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

void Core::Image::CreateImage(VkImageTiling tiling,
    VkImageUsageFlags usage, VkImageLayout initialLayout, VkImageCreateFlags flags)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = _extent;
    imageInfo.mipLevels = _mipLevels;
    imageInfo.arrayLayers = _layer;
    imageInfo.format = _format;
    imageInfo.tiling = tiling;
    imageInfo.samples = _sampleCount;

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

void Core::Image::CreateImageView(uint32_t mipLevels, VkImageViewType imageViewType,
    VkImageAspectFlags aspectFlags)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = _image;
    viewInfo.viewType = imageViewType;
    viewInfo.format = _format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = _layer;

    if (vkCreateImageView(_device.GetDevice(), &viewInfo, nullptr, &_imageView) != VK_SUCCESS)
    {
        throw runtime_error("failed to create texture image view!");
    }
}