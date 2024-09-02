#include "stdafx.h"
#include "RenderTarget.h"
#include "CommandBuffer.h"

Core::RenderTarget::RenderTarget(VkFormat format, VkImageLayout layout, VkImage image, VkDeviceMemory imageMemory, VkImageView imageView, VkImageUsageFlags usageFlags, VkSampleCountFlagBits sampleCount, VkSampler sampler)
	:Format(format), Layout(layout), Image(image), ImageMemory(imageMemory),
	ImageView(imageView), UsageFlags(usageFlags), SampleCount(sampleCount), Sampler(sampler)
{
}

VkDescriptorImageInfo Core::RenderTarget::GetDescriptorImageInfo()
{
	VkDescriptorImageInfo imageInfo{};

	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfo.imageView = ImageView;
	imageInfo.sampler = Sampler;

	return imageInfo;
}

void Core::RenderTarget::TransitionImageLayout(CommandBuffer& commandBuffer,
	VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask,
	VkPipelineStageFlags srcStagemask, VkPipelineStageFlags dstStageMask,
	VkImageLayout oldLayout, VkImageLayout newLayout)
{
	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = Image;

	barrier.subresourceRange.aspectMask = GetAspectMask();
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = srcAccessMask;
	barrier.dstAccessMask = dstAccessMask;

	vkCmdPipelineBarrier(
		commandBuffer.GetHandle(),
		srcStagemask, dstStageMask,
		0,
		0, nullptr,
		0, nullptr,
		1, &barrier
	);
}

VkImageAspectFlags Core::RenderTarget::GetAspectMask()
{
	if (Format == VK_FORMAT_D16_UNORM ||
		Format == VK_FORMAT_D32_SFLOAT)
		return VK_IMAGE_ASPECT_DEPTH_BIT;

	if (Format == VK_FORMAT_D16_UNORM_S8_UINT ||
		Format == VK_FORMAT_D24_UNORM_S8_UINT ||
		Format == VK_FORMAT_D32_SFLOAT_S8_UINT)
		return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

	return VK_IMAGE_ASPECT_COLOR_BIT;
}
