#pragma once

namespace Core
{
	/*enum ImageFormat
	{
		Default = 0,
		Grey = 1,
		Grey_alpha = 2,
		Rgb = 3,
		Rgb_alpha = 4
	};*/

	class Image
	{
	public:
		friend class Texture;
	public:
		Image(Device& device, string filePath, VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT, VkImageViewType imageViewType = VK_IMAGE_VIEW_TYPE_2D, VkImageCreateFlags flags = 0);

		//Creates a render target image
		Image(Device& device, VkImageCreateInfo& imageInfo, VkImageLayout layout, 
			VkImageAspectFlags aspectFlags, VkImageViewType imageViewType = VK_IMAGE_VIEW_TYPE_2D);

		~Image();

		VkFormat GetFormat() { return _format; }
		VkImage& GetImage() { return _image; }
		VkImageView& GetImageView() { return _imageView; }
		const VkExtent3D& GetExtent() { return _extent; }
		void SetSRGBFormat();

		VkSampleCountFlagBits GetSampleCount() { return _sampleCount; }

		uint32_t GetLayer() { return _layer; }
		uint32_t GetMipLevel() { return _mipLevels; }
		VkImageLayout GetLayout() { return _layout; }
		VkImageAspectFlags GetAspectFlags() const;
	private:
		void LoadRawImage(const string& filePath);
		void LoadStbImage(const string& filePath);
		void LoadKtxImage(const string& path);
		void CreateImage(VkImageTiling tiling, 
			VkImageUsageFlags usage, VkImageLayout initialLayout, VkImageCreateFlags flags);
		void BindImageMemory(VkMemoryPropertyFlags properties);
		void CreateImageView(uint32_t mipLevels, VkImageViewType imageViewType, VkImageAspectFlags aspectFlags);

		void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
		void GenerateMipmaps(uint32_t mipLevels);
	private:
		Device& _device;

		VkFormat _format;
		VkImage _image;
		VkImageView _imageView;
		VkDeviceMemory _imageMemory;
		VkExtent3D _extent;
		uint32_t _layer;
		uint32_t _mipLevels;
		VkSampleCountFlagBits _sampleCount;
		VkImageUsageFlags _usageFlags;
		VkImageLayout _layout;

		vector<uint8_t> _data;
	};
}