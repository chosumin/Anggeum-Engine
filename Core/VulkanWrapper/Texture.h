#pragma once

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

	class Texture
	{
	public:
		Texture(Device& device, string fileName, TextureFormat format);
		~Texture();

		VkDescriptorImageInfo GetDescriptorImageInfo();
	private:
		void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
		void CreateTextureImageView();
		void CreateTextureSampler();
		void GenerateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);

		void TransitionImageLayout(VkImage image, VkFormat format,
			VkImageLayout oldLayout, VkImageLayout newLayout, 
			uint32_t mipLevels);
	private:
		Device& _device;
		VkImage _textureImage;
		VkDeviceMemory _textureImageMemory;
		VkImageView _textureImageView;
		VkSampler _textureSampler;
		uint32_t _mipLevels;
	};
}

