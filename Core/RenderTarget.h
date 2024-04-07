#pragma once

struct RenderTarget
{
public:
	VkFormat Format;
	VkImageLayout Layout;
	VkImage Image;
	VkDeviceMemory ImageMemory;
	VkImageView ImageView;
	VkImageUsageFlags UsageFlags;
	VkSampleCountFlagBits SampleCount;
};