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
			VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);

		//hack : need abstract as Texture
		static VkFormat FindSupportedFormat(
			const vector<VkFormat>& candidates,
			VkImageTiling tiling,
			VkFormatFeatureFlags features);
		static bool HasStencilComponent(VkFormat format);
		static void CreateImage(
			uint32_t width, uint32_t height, VkFormat format,
			VkImageTiling tiling, VkImageUsageFlags usage,
			VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
		static void TransitionImageLayout(
			VkCommandPool commandPool, VkImage image, VkFormat format,
			VkImageLayout oldLayout, VkImageLayout newLayout);
	};
}

