#include "stdafx.h"
#include "TransferJob.h"

Core::VkImageJob::VkImageJob(Device& device, Image& dstImage, string filePath)
	:Job(JobType::TRANSFER), _device(device), _dstImage(dstImage), _filePath(filePath)
{
}

Core::VkImageJob::~VkImageJob() = default;

void Core::VkImageJob::Execute()
{
	// Already uploaded (VkImage created) — nothing to do.
	if (_dstImage.GetImage() != VK_NULL_HANDLE)
	{
		status = JobStatus::COMPLETE;
		return;
	}

	vector<uint8_t> imageData;
	_dstImage.Load(imageData);

	VkDeviceSize bufferSize = imageData.size();

	_stagingBuffer = make_unique<Core::Buffer>(_device,
		bufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		MemoryType::STAGE);

	_stagingBuffer->CopyBuffer(imageData.data(), bufferSize);

	commandBuffer->TransitionImageLayout(_dstImage, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	auto extent = _dstImage.GetExtent();
	commandBuffer->CopyBufferToImage(*_stagingBuffer, _dstImage, extent.width, extent.height);

	//hack : need to be pregenerated and stored in the texture file to improve loading speed.
	if (_dstImage.GetMipLevel() > 1)
		commandBuffer->GenerateMipmaps(_dstImage, _dstImage.GetMipLevel());

	status = JobStatus::COMPLETE;
}
