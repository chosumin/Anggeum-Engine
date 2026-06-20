#pragma once

namespace Core
{
	struct ImageCreateInfo
	{
		string filePath;
		VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
		VkImageViewType imageViewType = VK_IMAGE_VIEW_TYPE_2D;
		VkImageCreateFlags flags = 0;
		VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
	};

	struct MemoryAllocation;
	class Image
	{
	public:
		friend class Texture;
	public:
		Image(Device& device, ImageCreateInfo imageCreateInfo);

		//Creates a render target image
		Image(Device& device, VkImageCreateInfo& imageInfo, 
			VkImageAspectFlags aspectFlags, VkImageViewType imageViewType = VK_IMAGE_VIEW_TYPE_2D);

		~Image();

		VkFormat GetFormat() { return _format; }
		VkImage& GetImage() { return _image; }
		VkImageView& GetOrCreateImageView(uint mipLevel);
		VkImageView& GetOrCreateLayerImageView(uint32_t layerIndex);
		const VkExtent3D& GetExtent() { return _extent; }
		void SetSRGBFormat();

		VkSampleCountFlagBits GetSampleCount() { return _sampleCount; }

		uint32_t GetLayer() { return _layer; }
		uint32_t GetMipLevel() { return _mipLevels; }
		VkImageAspectFlags GetAspectFlags() const;

		void Load(vector<uint8_t>& outImageData);

		string& GetFilePath() { return _filePath; }
	private:
		void LoadRawImage(vector<uint8_t>& outData, const string& filePath);
		void LoadStbImage(vector<uint8_t>& outData, const string& filePath);
		void LoadHdrImage(vector<uint8_t>& outData, const string& filePath);
		void LoadKtxImage(vector<uint8_t>& outData, const string& path);
		void CreateImage(VkImageTiling tiling, 
			VkImageUsageFlags usage, VkImageLayout initialLayout, VkImageCreateFlags flags);
		void BindImageMemory(VkMemoryPropertyFlags properties);
		VkImageView CreateImageView(uint32_t mipLevels, VkImageViewType imageViewType, VkImageAspectFlags aspectFlags, uint32_t baseMipLevel);
		VkImageView CreateSingleLayerImageView(uint32_t layerIndex, VkImageAspectFlags aspectFlags);
	private:
		Device& _device;

		VkFormat _format;
		VkImage _image;

		VkImageView _imageView;
		vector<VkImageView> _mipImageViews;
		vector<VkImageView> _layerImageViews;

		VkExtent3D _extent;
		uint32_t _layer;
		uint32_t _mipLevels;
		VkSampleCountFlagBits _sampleCount;
		VkImageUsageFlags _usageFlags;
		VkImageCreateFlags _createFlags;
		VkImageViewType _viewType;

		unique_ptr<MemoryAllocation> _allocation;
		MemoryAllocatorManager* _allocator;

		string _filePath;
	};
}