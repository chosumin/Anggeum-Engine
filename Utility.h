#pragma once

namespace Core
{
	class Utility
	{
	public:
		static VkImageView CreateImageView(Device& device,
			VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);

		//hack : need abstract as Texture
		static bool HasStencilComponent(VkFormat format);
		static void CreateImage(Device& device,
			uint32_t width, uint32_t height, uint32_t mipLevels, 
			VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling,
			VkImageUsageFlags usage, VkMemoryPropertyFlags properties, 
			VkImage& image, VkDeviceMemory& imageMemory);

		static constexpr uint32_t HashCode(const char* str)
		{
			return str[0] ? static_cast<uint32_t>(str[0]) + 0xEDB8832Full * HashCode(str + 1) : 8603;
		}
	};
}

