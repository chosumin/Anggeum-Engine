#pragma once
#include "IDescriptor.h"

namespace Core
{
	enum TextureFormat
	{
		Default = 0,
		Grey = 1,
		Grey_alpha = 2,
		Rgb = 3,
		Rgb_alpha = 4
	};

	class Texture : public IDescriptor
	{
	public:
		Texture(VkCommandPool commandPool, string fileName, TextureFormat format);
		~Texture();

		VkDescriptorSetLayoutBinding CreateDescriptorSetLayoutBinding();
		VkWriteDescriptorSet CreateWriteDescriptorSet(size_t index);
		VkDescriptorType GetDescriptorType();
	private:
		void CreateImage(
			uint32_t width, uint32_t height, VkFormat format, 
			VkImageTiling tiling, VkImageUsageFlags usage, 
			VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
		void TransitionImageLayout(
			VkCommandPool commandPool, VkImage image, VkFormat format, 
			VkImageLayout oldLayout, VkImageLayout newLayout);
		void CopyBufferToImage(VkCommandPool commandPool, 
			VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
		void CreateTextureImageView();
		void CreateTextureSampler();
	private:
		VkImage _textureImage;
		VkDeviceMemory _textureImageMemory;
		VkImageView _textureImageView;
		VkSampler _textureSampler;
		VkDescriptorImageInfo _imageInfo;
	};
}

