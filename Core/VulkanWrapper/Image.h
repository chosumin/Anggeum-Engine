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
		Image(Device& device, string filePath, VkImageViewType imageViewType = VK_IMAGE_VIEW_TYPE_2D, VkImageCreateFlags flags = 0);

		~Image();

		VkFormat GetFormat() { return _format; }
		VkImage& GetImage() { return _image; }
		VkImageView& GetImageView() { return _imageView; }
		const VkExtent3D& GetExtent() { return _extent; }
		void SetSRGBFormat();
	private:
		void LoadRawImage(const string& filePath);
		void LoadStbImage(const string& filePath);
		void LoadKtxImage(const string& path);
		void CreateImage(VkExtent3D extent, 
			VkImageViewType imageViewType, VkImageCreateFlags flags);
		void CreateImage(VkExtent3D extent, uint32_t mipLevels, 
			VkSampleCountFlagBits numSamples, VkImageTiling tiling, 
			VkImageUsageFlags usage, VkImageLayout initialLayout, VkImageCreateFlags flags);
		void BindImageMemory(VkMemoryPropertyFlags properties);
		void CreateImageView(uint32_t mipLevels, VkImageViewType imageViewType);

		void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
		void TransitionImageLayout(VkImage image,
			VkImageLayout oldLayout, VkImageLayout newLayout,
			uint32_t mipLevels);
		void GenerateMipmaps(uint32_t mipLevels);
	private:
		Device& _device;
		VkFormat _format;
		VkImage _image;
		VkImageView _imageView;
		VkDeviceMemory _imageMemory;
		VkExtent3D _extent;
		uint32_t _layer;

		vector<uint8_t> _data;
	};
}