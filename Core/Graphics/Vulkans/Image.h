#pragma once

namespace Core
{
	// Description for file-based asset textures (they carry a path, not a shape).
	struct ImageCreateDesc
	{
		string filePath;
		VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
		VkImageViewType imageViewType = VK_IMAGE_VIEW_TYPE_2D;
		VkImageCreateFlags flags = 0;
		VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
	};

	// The one description for every engine-created image.
	struct ImageDesc
	{
		VkExtent2D extent{};
		uint32_t depth = 1; // > 1 makes a 3D image

		VkFormat format = VK_FORMAT_UNDEFINED;
		VkImageUsageFlags usage = 0;
		VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
		VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		uint32_t mipLevels = 1;
		uint32_t arrayLayers = 1;
		bool isCubemap = false;

		// MAX_ENUM = derive: cube if isCubemap, 2D_ARRAY if arrayLayers > 1,
		// 3D if depth > 1, else 2D.
		VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	};

	struct MemoryAllocation;
	class Image
	{
	public:
		friend class Texture;
	public:
		// Tag for the transient/aliased path: the VkImage is created without
		// memory.
		struct Unbound {};

		Image(Device& device, ImageCreateDesc imageCreateInfo);

		// The unified creation path for engine-made images: 
		// bound device-local memory + default view.
		Image(Device& device, const ImageDesc& desc);

		// Transient/aliased path:
		// the default view is deferred to BindMemoryAt.
		Image(Device& device, const ImageDesc& desc, Unbound);

		~Image();

		// View creation for images the engine does not own (the swapchain's).
		static VkImageView CreateRawView(Device& device, VkImage image,
			VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);

		VkMemoryRequirements GetMemoryRequirements() const;
		
		// Binds at an explicit offset into caller-owned memory (transient heap)
		// and creates the default image view. The memory must outlive this image;
		// the destructor does not free it.
		void BindMemoryAt(VkDeviceMemory memory, VkDeviceSize offset);

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

		// Loads the file and creates the VkImage. `outCopyRegions` is the
		// upload's per-mip/layer copy list when the file bakes one (KTX;
		// offsets relative to the staged blob), empty otherwise - transient
		// upload data for the caller, never stored here.
		void Load(vector<uint8_t>& outImageData,
			vector<VkBufferImageCopy>& outCopyRegions);

		static VkDeviceSize QueryStagingBytes(const string& filePath);

		// Prefers the baked KTX2 sibling the asset pipeline writes next to raw
		// png/jpg sources - mips come from the file and the upload stays a pure
		// copy. Returns the input unchanged when no sibling exists.
		static string ResolveBakedPath(const string& filePath);

		string& GetFilePath() { return _filePath; }
	private:
		void LoadRawImage(vector<uint8_t>& outData,
			vector<VkBufferImageCopy>& outCopyRegions, const string& filePath);
		void LoadStbImage(vector<uint8_t>& outData, const string& filePath);
		void LoadHdrImage(vector<uint8_t>& outData, const string& filePath);
		void LoadKtxImage(vector<uint8_t>& outData,
			vector<VkBufferImageCopy>& outCopyRegions, const string& path);
		void CreateImage(VkImageTiling tiling, 
			VkImageUsageFlags usage, VkImageLayout initialLayout, VkImageCreateFlags flags);
		void BindImageMemory(VkMemoryPropertyFlags properties);
		VkImageView CreateImageView(uint32_t mipLevels, VkImageViewType imageViewType, VkImageAspectFlags aspectFlags, uint32_t baseMipLevel);

		// Labels a freshly created view for the validation layer.
		void NameView(VkImageView view, const char* kind, uint32_t index) const;
		VkImageView CreateSingleLayerImageView(uint32_t layerIndex, VkImageAspectFlags aspectFlags);

		// View-type derivation shared by both ctors (see ImageDesc::viewType).
		static VkImageViewType DeriveViewType(const ImageDesc& desc);
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

		// Aspect requested at construction for the deferred-bind (Unbound) path;
		// the default view is created with it in BindMemoryAt.
		VkImageAspectFlags _deferredAspectFlags = 0;

		string _filePath;
	};
}