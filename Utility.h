#pragma once

namespace Core
{
	class Utility
	{
	public:
		static VkCommandBuffer BeginSingleTimeCommands(VkCommandPool commandPool);
		static void EndSingleTimeCommands(
			VkCommandPool commandPool, VkCommandBuffer commandBuffer);

		static VkImageView CreateImageView(
			VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);

		//hack : need abstract as Texture
		static VkFormat FindSupportedFormat(
			const vector<VkFormat>& candidates,
			VkImageTiling tiling,
			VkFormatFeatureFlags features);
		static bool HasStencilComponent(VkFormat format);
		static void CreateImage(
			uint32_t width, uint32_t height, uint32_t mipLevels, 
			VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling,
			VkImageUsageFlags usage, VkMemoryPropertyFlags properties, 
			VkImage& image, VkDeviceMemory& imageMemory);
		static void TransitionImageLayout(
			VkCommandPool commandPool, VkImage image, VkFormat format,
			VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels);
	};
}

