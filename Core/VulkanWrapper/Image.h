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
		Image(Device& device, vector<uint8_t>&& data);
		Image(Device& device, string filePath);

		~Image();

		VkImage& GetImage() { return _image; }
		VkImageView& GetImageView() { return _imageView; }
		const VkExtent3D& GetExtent() { return _extent; }
	private:
		void CreateImage(void* pixels, VkExtent3D extent);
		void CreateImage(VkExtent3D extent, uint32_t mipLevels, 
			VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, 
			VkImageUsageFlags usage, VkImageLayout initialLayout);
		void BindImageMemory(VkMemoryPropertyFlags properties);
		void CreateImageView(VkFormat format, uint32_t mipLevels);

		void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
		void TransitionImageLayout(VkImage image, VkFormat format,
			VkImageLayout oldLayout, VkImageLayout newLayout,
			uint32_t mipLevels);
	private:
		Device& _device;
		VkImage _image;
		VkImageView _imageView;
		VkDeviceMemory _imageMemory;
		VkExtent3D _extent;
	};
}