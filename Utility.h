#pragma once

namespace Core
{
	class Utility
	{
	public:
		static VkCommandBuffer BeginSingleTimeCommands(VkCommandPool commandPool);
		static void EndSingleTimeCommands(
			VkCommandPool commandPool, VkCommandBuffer commandBuffer);
		static VkImageView CreateImageView(VkImage image, VkFormat format);
	};
}

