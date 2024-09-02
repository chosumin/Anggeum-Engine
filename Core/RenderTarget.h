#pragma once

namespace Core
{
	class CommandBuffer;
	struct RenderTarget
	{
	public:
		RenderTarget(VkFormat format,
			VkImageLayout layout,
			VkImage image,
			VkDeviceMemory imageMemory,
			VkImageView imageView,
			VkImageUsageFlags usageFlags,
			VkSampleCountFlagBits sampleCount,
			VkSampler sampler);

		VkDescriptorImageInfo GetDescriptorImageInfo();

		void TransitionImageLayout(CommandBuffer& commandBuffer, 
			VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, 
			VkPipelineStageFlags srcStagemask, VkPipelineStageFlags dstStageMask, 
			VkImageLayout oldLayout, VkImageLayout newLayout);
	private:
		VkImageAspectFlags GetAspectMask();
	public:
		VkFormat Format;
		VkImageLayout Layout;
		VkImage Image;
		VkDeviceMemory ImageMemory;
		VkImageView ImageView;
		VkImageUsageFlags UsageFlags;
		VkSampleCountFlagBits SampleCount;
		VkSampler Sampler;
	};
}