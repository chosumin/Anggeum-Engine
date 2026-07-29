#include "stdafx.h"
#include "TransferJob.h"
#include "Graphics/Vulkans/Texture.h"

Core::VkImageJob::VkImageJob(Device& device, Texture& dstTexture, string filePath)
	:Job(JobType::TRANSFER), _device(device), _dstTexture(dstTexture), _filePath(filePath)
{
}

Core::VkImageJob::~VkImageJob() = default;

void Core::VkImageJob::Execute()
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

	_stagingBuffer = make_unique<Core::Buffer>(_device,
		bufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		MemoryType::STAGE);

	_stagingBuffer->CopyBuffer(imageData.data(), bufferSize);

	commandBuffer->CreateBarrierBatch()
		.Image(_dstTexture, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		.Submit();

	auto extent = image.GetExtent();
	commandBuffer->CopyBufferToImage(*_stagingBuffer, _dstTexture, extent.width, extent.height);

	//hack : need to be pregenerated and stored in the texture file to improve loading speed.
	if (image.GetMipLevel() > 1)
		commandBuffer->GenerateMipmaps(_dstTexture, image.GetMipLevel());

	status = JobStatus::COMPLETE;
}
