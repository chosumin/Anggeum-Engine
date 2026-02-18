#include "stdafx.h"
#include "Image.h"
#include "Buffer.h"
#include "CommandBuffer.h"
#include "MemoryAllocator.h"
#include "Utils/FileSystem.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <ktx.h>
#include <ktxvulkan.h>

Core::Image::Image(Device& device, ImageCreateInfo imageCreateInfo)
    :_device(device), _sampleCount(imageCreateInfo.sampleCount), _createFlags(imageCreateInfo.flags), _viewType(imageCreateInfo.imageViewType),
    _filePath(imageCreateInfo.filePath),
    _image(VK_NULL_HANDLE), _imageView(VK_NULL_HANDLE)
{
    _format = VK_FORMAT_R8G8B8A8_UNORM;
    _usageFlags = 
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | 
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | 
        VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_STORAGE_BIT;
}

Core::Image::Image(Device& device, VkImageCreateInfo& imageInfo, 
    VkImageAspectFlags aspectFlags, VkImageViewType imageViewType)
	:_device(device), _format(imageInfo.format), _extent(imageInfo.extent), _sampleCount(imageInfo.samples), _mipLevels(imageInfo.mipLevels), _usageFlags(imageInfo.usage),
	_layer(imageInfo.arrayLayers), _viewType(imageViewType)
{
	CreateImage(
		VK_IMAGE_TILING_OPTIMAL,
		_usageFlags,
		VK_IMAGE_LAYOUT_UNDEFINED,
        imageInfo.flags);

	BindImageMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    _imageView = CreateImageView(_mipLevels,
        imageViewType, 
        aspectFlags, 0);
}

Core::Image::~Image()
{
    auto device = _device.GetDevice();

	if (_imageView != VK_NULL_HANDLE)
		vkDestroyImageView(device, _imageView, nullptr);

    for(auto& mipView : _mipImageViews)
    {
        if (mipView != VK_NULL_HANDLE)
            vkDestroyImageView(device, mipView, nullptr);
	}

	if (_image != VK_NULL_HANDLE)
		vkDestroyImage(device, _image, nullptr);

	if (_allocation != nullptr)
		_allocator->Deallocate(*_allocation);
}

VkImageView& Core::Image::GetOrCreateImageView(uint mipLevel)
{
	if (mipLevel == 0)
		return _imageView;

    if (_mipImageViews.size() == 0)
        _mipImageViews.resize(_mipLevels - 1);

    if (_mipImageViews[mipLevel - 1] != VK_NULL_HANDLE)
        return _mipImageViews[mipLevel - 1];

    auto imageView = CreateImageView(1,
        _viewType,
        GetAspectFlags(),
        mipLevel);
	_mipImageViews[mipLevel - 1] = imageView;

	return _mipImageViews[mipLevel - 1];
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

void Core::Image::LoadRawImage(vector<uint8_t>& data, const string& filePath)
{
    string extension = FileSystem::GetExtension(filePath);

    if (extension == "ktx")
    {
        LoadKtxImage(data, filePath);
    }
    else if (extension == "png" || extension == "jpg")
    {
        LoadStbImage(data, filePath);
    }
}

void Core::Image::LoadStbImage(vector<uint8_t>& data, const string& filePath)
{
    int width, height, comp;
    int reqComp = 4;

    stbi_uc* pixels = stbi_load(filePath.c_str(),
        &width, &height, &comp, static_cast<int>(reqComp));

    if (pixels == nullptr)
        throw runtime_error("failed to load texture image!");

    data = { pixels, pixels + (width * height * reqComp) };

    _extent.depth = 1u;
    _extent.width = static_cast<uint32_t>(width);
    _extent.height = static_cast<uint32_t>(height);

    _layer = 1;

    stbi_image_free(pixels);
}

void Core::Image::LoadKtxImage(vector<uint8_t>& outData, const string& path)
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
        outData = { texture->pData , texture->pData + texture->dataSize };
    }
    else
    {
        ktx_size_t dataSize = texture->dataSize;
        outData.resize(dataSize);
        auto loadDataResult = ktxTexture_LoadImageData(texture, outData.data(), dataSize);

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

    auto result = vkCreateImage(_device.GetDevice(), &imageInfo, nullptr, &_image);
    if (result != VK_SUCCESS)
    {
        throw runtime_error("failed to create image!");
    }
}

void Core::Image::BindImageMemory(VkMemoryPropertyFlags properties)
{
    auto device = _device.GetDevice();

    VkMemoryDedicatedRequirements dedicatedReqs{};
    dedicatedReqs.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
    dedicatedReqs.pNext = nullptr;

	VkMemoryRequirements2 memRequirements{};
	memRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
	memRequirements.pNext = &dedicatedReqs;

	VkImageMemoryRequirementsInfo2 imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
    imageInfo.pNext = nullptr;
	imageInfo.image = _image;

    vkGetImageMemoryRequirements2(device, &imageInfo, &memRequirements);

    bool needDedicated = dedicatedReqs.prefersDedicatedAllocation ||
        dedicatedReqs.requiresDedicatedAllocation;

    _allocator = _device.GetMemoryAllocatorManager();

    _allocation = make_unique<MemoryAllocation>();
    _allocator->Allocate(*_allocation, MemoryType::IMAGE, memRequirements.memoryRequirements.size, needDedicated);
    _allocator->BindImageMemory(*this, *_allocation);
}

VkImageView Core::Image::CreateImageView(uint32_t mipLevels, VkImageViewType imageViewType,
    VkImageAspectFlags aspectFlags, uint32_t baseMipLevel)
{
	VkImageView imageView;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = _image;
    viewInfo.viewType = imageViewType;
    viewInfo.format = _format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = baseMipLevel;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = _layer;

    if (vkCreateImageView(_device.GetDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS)
    {
        throw runtime_error("failed to create texture image view!");
    }

    return imageView;
}

void Core::Image::Load(vector<uint8_t>& outImageData)
{
    if (_filePath.empty())
        return;

    LoadRawImage(outImageData, _filePath);

    _mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(_extent.width, _extent.height)))) + 1;

    CreateImage(
        VK_IMAGE_TILING_OPTIMAL,
        _usageFlags,
        VK_IMAGE_LAYOUT_UNDEFINED,
        _createFlags);

    BindImageMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    _imageView = CreateImageView(_mipLevels, _viewType, VK_IMAGE_ASPECT_COLOR_BIT, 0);
}