#include "stdafx.h"
#include "Image.h"
#include "Buffer.h"
#include "CommandBuffer.h"
#include "MemoryAllocator.h"
#include "Graphics/FrameResources.h"
#include "Utils/FileSystem.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <ktx.h>
#include <ktxvulkan.h>

Core::Image::Image(Device& device, ImageCreateDesc imageCreateInfo)
    :_device(device), _sampleCount(imageCreateInfo.sampleCount), _createFlags(imageCreateInfo.flags), _viewType(imageCreateInfo.imageViewType),
	_filePath(imageCreateInfo.filePath), _format(imageCreateInfo.format),
    _image(VK_NULL_HANDLE), _imageView(VK_NULL_HANDLE)
{
    _usageFlags = 
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | 
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | 
        VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_STORAGE_BIT;
}

VkImageViewType Core::Image::DeriveViewType(const ImageDesc& desc)
{
	if (desc.viewType != VK_IMAGE_VIEW_TYPE_MAX_ENUM)
		return desc.viewType;
	if (desc.isCubemap)
		return VK_IMAGE_VIEW_TYPE_CUBE;
	if (desc.arrayLayers > 1)
		return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
	if (desc.depth > 1)
		return VK_IMAGE_VIEW_TYPE_3D;
	return VK_IMAGE_VIEW_TYPE_2D;
}

Core::Image::Image(Device& device, const ImageDesc& desc)
	:_device(device), _format(desc.format),
	_extent{ desc.extent.width, desc.extent.height, desc.depth },
	_sampleCount(desc.samples), _mipLevels(desc.mipLevels), _usageFlags(desc.usage),
	_layer(desc.arrayLayers), _viewType(DeriveViewType(desc)),
	_createFlags(desc.isCubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0)
{
	CreateImage(
		VK_IMAGE_TILING_OPTIMAL,
		_usageFlags,
		VK_IMAGE_LAYOUT_UNDEFINED,
		_createFlags);

	BindImageMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	_imageView = CreateImageView(_mipLevels, _viewType, desc.aspect, 0);
}

Core::Image::Image(Device& device, const ImageDesc& desc, Unbound)
	:_device(device), _format(desc.format),
	_extent{ desc.extent.width, desc.extent.height, desc.depth },
	_sampleCount(desc.samples), _mipLevels(desc.mipLevels), _usageFlags(desc.usage),
	_layer(desc.arrayLayers), _viewType(DeriveViewType(desc)),
	_createFlags(desc.isCubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0),
	_image(VK_NULL_HANDLE), _imageView(VK_NULL_HANDLE), _deferredAspectFlags(desc.aspect)
{
	CreateImage(
		VK_IMAGE_TILING_OPTIMAL,
		_usageFlags,
		VK_IMAGE_LAYOUT_UNDEFINED,
		_createFlags);
}

VkImageView Core::Image::CreateRawView(Device& device, VkImage image,
	VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels)
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
		throw runtime_error("failed to create raw image view!");

	return imageView;
}

VkMemoryRequirements Core::Image::GetMemoryRequirements() const
{
	VkMemoryRequirements requirements{};
	vkGetImageMemoryRequirements(_device.GetDevice(), _image, &requirements);
	return requirements;
}

void Core::Image::BindMemoryAt(VkDeviceMemory memory, VkDeviceSize offset)
{
	assert(_allocation == nullptr && "image already owns a managed allocation");
	assert(_imageView == VK_NULL_HANDLE && "image memory already bound");

	if (vkBindImageMemory(_device.GetDevice(), _image, memory, offset) != VK_SUCCESS)
		throw runtime_error("failed to bind image memory at offset!");

	// Views require bound memory, so the default view is created here rather
	// than in the constructor.
	_imageView = CreateImageView(_mipLevels, _viewType, _deferredAspectFlags, 0);
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

    for (auto& layerView : _layerImageViews)
    {
        if (layerView != VK_NULL_HANDLE)
            vkDestroyImageView(device, layerView, nullptr);
    }

	if (_image != VK_NULL_HANDLE)
		vkDestroyImage(device, _image, nullptr);

	if (_allocation != nullptr)
		_device.GetMemoryAllocatorManager()->Deallocate(*_allocation);
}

VkImageView& Core::Image::GetOrCreateImageView(uint mipLevel)
{
	if (mipLevel == 0)
		return _imageView;

    if (_mipImageViews.size() == 0)
        _mipImageViews.resize(_mipLevels - 1);

    if (_mipImageViews[mipLevel - 1] != VK_NULL_HANDLE)
        return _mipImageViews[mipLevel - 1];

    // Passes record in parallel, so creating a view here would race: two workers
    // would each create one and leak the loser (and corrupt the vector). Whoever
    // needs a mip view must ask for it from Setup.
    assert(!FrameResources::IsRecordingGuardActive() &&
        "mip image view created during the recording window; create it in Setup");

    auto imageView = CreateImageView(1,
        _viewType,
        GetAspectFlags(),
        mipLevel);
	_mipImageViews[mipLevel - 1] = imageView;

	return _mipImageViews[mipLevel - 1];
}

VkImageView& Core::Image::GetOrCreateLayerImageView(uint32_t layerIndex)
{
    if (layerIndex >= _layer)
        throw runtime_error("Layer index out of range!");

    if (_layerImageViews.empty())
        _layerImageViews.resize(_layer, VK_NULL_HANDLE);

    if (_layerImageViews[layerIndex] != VK_NULL_HANDLE)
        return _layerImageViews[layerIndex];

    assert(!FrameResources::IsRecordingGuardActive() &&
        "layer image view created during the recording window; create it in Setup");

    _layerImageViews[layerIndex] = CreateSingleLayerImageView(layerIndex, GetAspectFlags());
    return _layerImageViews[layerIndex];
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

string Core::Image::ResolveBakedPath(const string& filePath)
{
    const size_t dot = filePath.rfind('.');
    if (dot == string::npos)
        return filePath;

    const string extension = filePath.substr(dot + 1);
    if (extension != "png" && extension != "jpg")
        return filePath;

    string baked = filePath.substr(0, dot) + ".ktx2";
    return FileSystem::Exists(baked) ? baked : filePath;
}

void Core::Image::LoadRawImage(vector<uint8_t>& data,
    vector<VkBufferImageCopy>& outCopyRegions, const string& filePath)
{
    string extension = FileSystem::GetExtension(filePath);

    if (extension == "ktx" || extension == "ktx2")
    {
        LoadKtxImage(data, outCopyRegions, filePath);
    }
    else if (extension == "hdr")
    {
        LoadHdrImage(data, filePath);
    }
    else if (extension == "png" || extension == "jpg")
    {
        LoadStbImage(data, filePath);
    }
}

void Core::Image::LoadHdrImage(vector<uint8_t>& outData, const string& filePath)
{
    int width, height, comp;
    constexpr int reqComp = 4; // RGBA

    float* pixels = stbi_loadf(filePath.c_str(), &width, &height, &comp, reqComp);

    if (pixels == nullptr)
        throw runtime_error("failed to load HDR image: " + filePath);

    _extent.depth = 1u;
    _extent.width = static_cast<uint32_t>(width);
    _extent.height = static_cast<uint32_t>(height);
    _layer = 1;
    _mipLevels = 1;

    _format = VK_FORMAT_R32G32B32A32_SFLOAT;

    const size_t byteSize = static_cast<size_t>(width) * height * reqComp * sizeof(float);
    const uint8_t* byteData = reinterpret_cast<const uint8_t*>(pixels);
    outData = { byteData, byteData + byteSize };

    stbi_image_free(pixels);
}

VkDeviceSize Core::Image::QueryStagingBytes(const string& filePath)
{
	const string extension = FileSystem::GetExtension(filePath);
	if (extension == "ktx" || extension == "ktx2")
	{
		// Header-only open: with NO_FLAGS the pixel data stays on disk. The
		// UNCOMPRESSED size is what LoadKtxImage stages (supercompression is
		// inflated at load), so that is what the budget must charge.
		ktxTexture* texture = nullptr;
		if (ktxTexture_CreateFromNamedFile(filePath.c_str(),
			KTX_TEXTURE_CREATE_NO_FLAGS, &texture) == KTX_SUCCESS)
		{
			VkDeviceSize size = ktxTexture_GetDataSizeUncompressed(texture);
			ktxTexture_Destroy(texture);
			return size;
		}
	}
	else
	{
		int width = 0, height = 0, comp = 0;
		if (stbi_info(filePath.c_str(), &width, &height, &comp))
		{
			VkDeviceSize texelBytes = stbi_is_hdr(filePath.c_str()) ? 16 : 4;
			return VkDeviceSize(width) * VkDeviceSize(height) * texelBytes;
		}
	}

	std::error_code ec;
	uintmax_t size = std::filesystem::file_size(filePath, ec);
	return ec ? VkDeviceSize(1) << 20 : VkDeviceSize(size);
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

    _mipLevels = 1;

    stbi_image_free(pixels);
}

void Core::Image::LoadKtxImage(vector<uint8_t>& outData,
    vector<VkBufferImageCopy>& outCopyRegions, const string& path)
{
    auto data = FileSystem::Read(path);

    auto dataBuffer = reinterpret_cast<const ktx_uint8_t*>(data.data());
    auto dataSize = static_cast<ktx_size_t>(data.size());

    // LOAD_IMAGE_DATA inflates any supercompression (the baked assets are
    // zstd-wrapped) into pData; image offsets below index the inflated blob.
    ktxTexture* texture;
    auto ktxResult = ktxTexture_CreateFromMemory(
        dataBuffer, dataSize,
        KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);

    if (ktxResult != KTX_SUCCESS)
    {
        throw runtime_error{ "Error loading KTX texture: " + path };
    }

    // Basis-encoded payloads (the baked UASTC assets) transcode right here 
    // to the desktop block format, and the image honors the
    // transcoded format over the desc's placeholder.
    if (texture->classId == ktxTexture2_c)
    {
        auto* texture2 = reinterpret_cast<ktxTexture2*>(texture);
        if (ktxTexture2_NeedsTranscoding(texture2))
        {
            if (ktxTexture2_TranscodeBasis(texture2, KTX_TTF_BC7_RGBA, 0) != KTX_SUCCESS)
                throw runtime_error{ "Error transcoding KTX texture: " + path };

            _format = ktxTexture_GetVkFormat(texture);
        }
    }

    outData = { texture->pData, texture->pData + texture->dataSize };

    _extent.depth = texture->baseDepth;
    _extent.width = texture->baseWidth;
    _extent.height = texture->baseHeight;

    _layer = texture->numLayers;

    // Use the faces if there are 6 (for cubemap)
    const bool cubemap = texture->numLayers == 1 && texture->numFaces == 6;
    if (cubemap)
    {
        _layer = texture->numFaces;
    }

    // The file's baked mip chain replaces runtime generation: keep its level
    // count and hand back one copy region per mip/layer at the blob's own
    // offsets. (Rows are assumed tightly packed, which holds for the 4-byte
    // and block-compressed formats KTX assets use.)
    _mipLevels = texture->numLevels;

    outCopyRegions.clear();
    for (uint32_t level = 0; level < texture->numLevels; ++level)
    {
        for (uint32_t layer = 0; layer < _layer; ++layer)
        {
            ktx_size_t offset = 0;
            ktxTexture_GetImageOffset(texture, level,
                cubemap ? 0 : layer, cubemap ? layer : 0, &offset);

            VkBufferImageCopy region{};
            region.bufferOffset = offset;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = level;
            region.imageSubresource.baseArrayLayer = layer;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {
                std::max(1u, texture->baseWidth >> level),
                std::max(1u, texture->baseHeight >> level),
                std::max(1u, texture->baseDepth >> level) };
            outCopyRegions.push_back(region);
        }
    }

    ktxTexture_Destroy(texture);
}

void Core::Image::CreateImage(VkImageTiling tiling,
    VkImageUsageFlags usage, VkImageLayout initialLayout, VkImageCreateFlags flags)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;

    // Determine image type from extent depth
    if (_extent.depth > 1)
        imageInfo.imageType = VK_IMAGE_TYPE_3D;
    else
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

    // CONCURRENT across every distinct family that touches this image, so
    // cross-queue access needs no ownership transfers: graphics+compute
    // always; the dedicated transfer family only when the usage says the
    // upload lane may write it (render targets keep their g+c list).
    const auto& qfi = _device.GetQueueFamilyIndices();
    uint32_t queueFamilies[3];
    uint32_t familyCount = 0;
    auto addUnique = [&](uint32_t family)
    {
        for (uint32_t i = 0; i < familyCount; ++i)
            if (queueFamilies[i] == family)
                return;
        queueFamilies[familyCount++] = family;
    };
    addUnique(qfi.GraphicsFamily.value());
    addUnique(qfi.ComputeFamily.value());
    if (usage & (VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
        addUnique(qfi.TransferFamily.value());

    if (familyCount > 1)
    {
        imageInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
        imageInfo.queueFamilyIndexCount = familyCount;
        imageInfo.pQueueFamilyIndices = queueFamilies;
    }
    else
    {
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

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

    auto* allocator = _device.GetMemoryAllocatorManager();

    _allocation = make_unique<MemoryAllocation>();
    allocator->Allocate(*_allocation, MemoryType::IMAGE, memRequirements.memoryRequirements.size, needDedicated);
    allocator->BindImageMemory(*this, *_allocation);
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

    NameView(imageView, "mip", baseMipLevel);

    return imageView;
}

// Every view this class creates gets a name, so a validation message naming an
// unnamed view points at a view the engine did not create.
void Core::Image::NameView(VkImageView view, const char* kind, uint32_t index) const
{
    const string label = "Image(" + (_filePath.empty() ? string("rt") : _filePath) + ") "
        + kind + " " + std::to_string(index) + " View";

    _device.GetDebugUtils().SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW,
        (uint64_t)view, label.c_str());
}

VkImageView Core::Image::CreateSingleLayerImageView(uint32_t layerIndex, VkImageAspectFlags aspectFlags)
{
    VkImageView imageView;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = _image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = _format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = layerIndex;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(_device.GetDevice(), &viewInfo, nullptr, &imageView) != VK_SUCCESS)
    {
        throw runtime_error("failed to create single layer image view!");
    }

    NameView(imageView, "layer", layerIndex);

    return imageView;
}

void Core::Image::Load(vector<uint8_t>& outImageData,
    vector<VkBufferImageCopy>& outCopyRegions)
{
    if (_filePath.empty())
        return;

    LoadRawImage(outImageData, outCopyRegions, _filePath);

    // Block-compressed formats (transcoded BC) have no STORAGE image support;
    // file textures are sampled-only anyway.
    if (_format >= VK_FORMAT_BC1_RGB_UNORM_BLOCK && _format <= VK_FORMAT_BC7_SRGB_BLOCK)
        _usageFlags &= ~VK_IMAGE_USAGE_STORAGE_BIT;

    CreateImage(
        VK_IMAGE_TILING_OPTIMAL,
        _usageFlags,
        VK_IMAGE_LAYOUT_UNDEFINED,
        _createFlags);

    BindImageMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    _imageView = CreateImageView(_mipLevels, _viewType, VK_IMAGE_ASPECT_COLOR_BIT, 0);
}
