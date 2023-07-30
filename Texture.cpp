#include "stdafx.h"
#include "Texture.h"
#include "Buffer.h"
#include "Utility.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

//todo : https://developer.nvidia.com/vulkan-memory-management
//todo : https://en.wikipedia.org/wiki/Memory-mapped_I/O_and_port-mapped_I/O
//todo : https://registry.khronos.org/vulkan/specs/1.3-extensions/html/chap7.html#VkPipelineStageFlagBits
//todo : https://en.wikipedia.org/wiki/Anisotropic_filtering

Core::Texture::Texture(VkCommandPool commandPool, string fileName, TextureFormat format)
{
	int texWidth, texHeight, texChannels;

	stbi_uc* pixels = stbi_load(fileName.c_str(),
		&texWidth, &texHeight, &texChannels, static_cast<int>(format));

	VkDeviceSize imageSize = texWidth * texHeight * 4;

	if (pixels == nullptr)
		throw runtime_error("failed to load texture image!");

	Buffer stagingBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	stagingBuffer.CopyBuffer(pixels, imageSize);

	stbi_image_free(pixels);

	CreateImage(texWidth, texHeight,
		VK_FORMAT_R8G8B8A8_SRGB,
		VK_IMAGE_TILING_OPTIMAL, //VK_IMAGE_TILING_LINEAR to directly access texels in the memory.
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		_textureImage, _textureImageMemory);

	TransitionImageLayout(commandPool,
		_textureImage,
		VK_FORMAT_R8G8B8A8_SRGB,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	CopyBufferToImage(commandPool,
		stagingBuffer.GetBuffer(),
		_textureImage,
		static_cast<uint32_t>(texWidth),
		static_cast<uint32_t>(texHeight));

	TransitionImageLayout(commandPool, 
		_textureImage, 
		VK_FORMAT_R8G8B8A8_SRGB, 
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	CreateTextureImageView();
	CreateTextureSampler();
}

Core::Texture::~Texture()
{
	auto device = Device::Instance().GetDevice();

	vkDestroySampler(device, _textureSampler, nullptr);
	vkDestroyImageView(device, _textureImageView, nullptr);
	vkDestroyImage(device, _textureImage, nullptr);
	vkFreeMemory(device, _textureImageMemory, nullptr);
}

VkDescriptorSetLayoutBinding Core::Texture::CreateDescriptorSetLayoutBinding()
{
	VkDescriptorSetLayoutBinding samplerLayoutBinding{};
	samplerLayoutBinding.binding = 1;
	samplerLayoutBinding.descriptorCount = 1;
	samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerLayoutBinding.pImmutableSamplers = nullptr;
	samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	return samplerLayoutBinding;
}

VkWriteDescriptorSet Core::Texture::CreateWriteDescriptorSet(size_t index)
{
	_imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	_imageInfo.imageView = _textureImageView;
	_imageInfo.sampler = _textureSampler;

	VkWriteDescriptorSet descriptorWrite{};
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstBinding = 1;
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pImageInfo = &_imageInfo;

	return descriptorWrite;
}

VkDescriptorType Core::Texture::GetDescriptorType()
{
	return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
}

void Core::Texture::CreateImage(
	uint32_t width, uint32_t height, VkFormat format, 
	VkImageTiling tiling, VkImageUsageFlags usage, 
	VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory)
{
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = format;
	imageInfo.tiling = tiling;

	//VK_IMAGE_LAYOUT_PREINITIALIZED, the first transition will preserve the texels.
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = usage;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	//only relevant for images that will be used as attachments.
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

	//related to sparse images, such as 3D texture for a voxel terrain.
	imageInfo.flags = 0;

	auto device = Device::Instance().GetDevice();

	if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS)
	{
		throw runtime_error("failed to create image!");
	}

	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(device, image, &memRequirements);

	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex =
		Device::Instance().FindMemoryType(memRequirements.memoryTypeBits, properties);

	if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS)
	{
		throw runtime_error("failed to allocate image memory!");
	}

	vkBindImageMemory(device, image, imageMemory, 0);
}

void Core::Texture::TransitionImageLayout(
	VkCommandPool commandPool, VkImage image, VkFormat format,
	VkImageLayout oldLayout, VkImageLayout newLayout)
{
	VkCommandBuffer commandBuffer = Utility::BeginSingleTimeCommands(commandPool);

	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = 0; // TODO
	barrier.dstAccessMask = 0; // TODO

	VkPipelineStageFlags sourceStage;
	VkPipelineStageFlags destinationStage;

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
		commandBuffer,
		sourceStage, destinationStage,
		0,
		0, nullptr,
		0, nullptr,
		1, &barrier
	);

	Utility::EndSingleTimeCommands(commandPool, commandBuffer);
}

void Core::Texture::CopyBufferToImage(VkCommandPool commandPool, 
	VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
{
	VkCommandBuffer commandBuffer = Utility::BeginSingleTimeCommands(commandPool);

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

	vkCmdCopyBufferToImage(
		commandBuffer,
		buffer,
		image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1,
		&region
	);

	Utility::EndSingleTimeCommands(commandPool, commandBuffer);
}

void Core::Texture::CreateTextureImageView()
{
	_textureImageView = Utility::CreateImageView(_textureImage, VK_FORMAT_R8G8B8A8_SRGB);
}

void Core::Texture::CreateTextureSampler()
{
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

	VkPhysicalDeviceProperties properties{};
	vkGetPhysicalDeviceProperties(Device::Instance().GetPhysicalDevice(), &properties);

	samplerInfo.anisotropyEnable = VK_TRUE;
	//lower value results in better performance, but lower quality results.
	samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;

	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable = VK_FALSE; //usually used for percentage-closer filtering on shadow maps.
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = 0.0f;

	if (vkCreateSampler(Device::Instance().GetDevice(), &samplerInfo, nullptr, &_textureSampler) != VK_SUCCESS)
	{
		throw runtime_error("failed to create texture sampler!");
	}
}
