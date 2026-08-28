#include "stdafx.h"
#include "TextureUpload.h"
#include "StagingRing.h"
#include "Graphics/Vulkans/Texture.h"
#include "Graphics/Vulkans/Image.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/MemoryAllocator.h"

using namespace Core;

TextureUploadJob::TextureUploadJob(Device& device, Texture& dstTexture, string filePath)
	: UploadJob()
	, _device(device)
	, _filePath(filePath)
	, _dstTexture(dstTexture)
{
}

TextureUploadJob::~TextureUploadJob() = default;

void TextureUploadJob::Execute()
{
	// The command methods take the Texture&; the Image is only needed for the
	// CPU-side file load and dimension queries.
	Image& image = _dstTexture.GetImage();

	// Already uploaded (VkImage created) — nothing to do.
	if (image.GetImage() != VK_NULL_HANDLE)
	{
		status = JobStatus::COMPLETE;
		return;
	}

	vector<uint8_t> imageData;
	image.Load(imageData);

	VkDeviceSize bufferSize = imageData.size();

	StagingRing::Span span = stagingRing != nullptr
		? stagingRing->Acquire(bufferSize) : StagingRing::Span{};

	Buffer* source = nullptr;
	VkDeviceSize sourceOffset = 0;
	if (span.IsValid())
	{
		memcpy(span.mapped, imageData.data(), imageData.size());
		source = span.buffer;
		sourceOffset = span.offset;
		stagingSpanId = span.id; // closed by the submitter with its value
	}
	else
	{
		// Ring full (typical on the initial load spike) or absent: dedicated
		// staging, freed with the job.
		_stagingBuffer = make_unique<Core::Buffer>(_device,
			bufferSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			MemoryType::STAGE);
		_stagingBuffer->CopyBuffer(imageData.data(), bufferSize);
		source = _stagingBuffer.get();
	}

	commandBuffer->CreateBarrierBatch()
		.Image(_dstTexture, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		.Submit();

	auto extent = image.GetExtent();
	commandBuffer->CopyBufferToImage(*source, _dstTexture, extent.width, extent.height,
		sourceOffset);

	//hack : need to be pregenerated and stored in the texture file to improve loading speed.
	if (image.GetMipLevel() > 1)
		commandBuffer->GenerateMipmaps(_dstTexture, image.GetMipLevel());

	status = JobStatus::COMPLETE;
}
