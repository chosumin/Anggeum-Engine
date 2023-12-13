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
		void CopyBufferToImage(VkCommandPool commandPool, 
			VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
		void CreateTextureImageView();
		void CreateTextureSampler();
		void GenerateMipmaps(VkCommandPool commandPool,
			VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);
	private:
		VkImage _textureImage;
		VkDeviceMemory _textureImageMemory;
		VkImageView _textureImageView;
		VkSampler _textureSampler;
		VkDescriptorImageInfo _imageInfo;
		uint32_t _mipLevels;
	};
}

