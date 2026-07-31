#pragma once

namespace Core
{
	// Virtual resource handles used during frame graph setup.
	struct FGHandle
	{
		uint32_t index = UINT32_MAX;
		bool IsValid() const { return index != UINT32_MAX; }
	};

	struct FGTexture : FGHandle {};
	struct FGBuffer : FGHandle {};

	// How a pass touches a texture.
	enum class TextureAccess : uint8_t
	{
		ColorWrite,          // color attachment, cleared/overwritten
		ColorLoadWrite,      // color attachment, previous contents loaded
		DepthWrite,          // depth attachment, cleared/overwritten
		DepthLoadWrite,      // depth attachment, previous contents loaded
		DepthRead,           // depth attachment, read-only depth test
		SampledFragment,     // sampled in fragment shaders
		SampledVertex,       // sampled in vertex shaders
		SampledCompute,      // sampled in compute shaders
		StorageComputeWrite, // storage image write in compute (GENERAL)
		StorageComputeRead,  // storage image read in compute (GENERAL)
		TransferSrc,
		TransferDst,
		Present,             // final swapchain hand-off
	};

	// How a pass touches a buffer.
	enum class BufferAccess : uint8_t
	{
		UniformVertex,
		UniformFragment,
		UniformCompute,
		StorageVertexRead,
		StorageFragmentRead,
		StorageComputeRead,
		StorageComputeWrite,
		IndirectRead,
		TransferSrc,
		TransferDst,
	};

	// The sync2 facts a declared access boils down to.
	struct FGAccessInfo
	{
		VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
		VkAccessFlags2 access = VK_ACCESS_2_NONE;
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED; // meaningless for buffers
		bool isWrite = false;

		// The pass records its own barriers for this access (e.g. RenderExecutor's
		// mid-pass depth resolve). The graph emits no barrier and just adopts
		// layout/stage/access as the state the pass leaves the resource in.
		bool manualBarriers = false;
	};

	FGAccessInfo GetAccessInfo(TextureAccess access);
	FGAccessInfo GetAccessInfo(BufferAccess access);

	struct FGTextureDesc
	{
		VkExtent2D extent{};
		VkFormat format = VK_FORMAT_UNDEFINED;
		VkImageUsageFlags usage = 0;
		VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
		VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
		uint32_t mipLevels = 1;
		uint32_t arrayLayers = 1;
	};

	struct FGBufferDesc
	{
		VkDeviceSize size = 0;
		VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	};

	// Dynamic-rendering attachment declaration.
	struct FGAttachment
	{
		FGTexture texture;
		VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		VkClearValue clear{};
		FGTexture resolveTarget{}; // invalid = no resolve
	};
}
