#include "stdafx.h"
#include "TransferJob.h"

Core::VkImageJob::VkImageJob(Device& device, weak_ptr<Image> dstImage, string filePath)
	:Job(JobType::TRANSFER), _device(device), _dstImage(dstImage), _filePath(filePath)
{
}

Core::VkImageJob::~VkImageJob()
{
	if (_stagingBuffer != nullptr)
		delete(_stagingBuffer);
}

void Core::VkImageJob::Execute()
{
	auto sharedImage = _dstImage.lock();
	if (sharedImage == nullptr ||
		sharedImage->GetImage() != VK_NULL_HANDLE)
	{
		status = JobStatus::COMPLETE;
		return;
	}

	vector<uint8_t> imageData;
	sharedImage->Load(imageData);

	VkDeviceSize bufferSize = imageData.size();

	_stagingBuffer = new Core::Buffer(_device,
		bufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		MemoryType::STAGE);

	_stagingBuffer->CopyBuffer(imageData.data(), bufferSize);

	commandBuffer->TransitionImageLayout(*sharedImage, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	auto extent = sharedImage->GetExtent();
	commandBuffer->CopyBufferToImage(*_stagingBuffer, *sharedImage, extent.width, extent.height);

	//hack : need to be pregenerated and stored in the texture file to improve loading speed.
	if (sharedImage->GetMipLevel() > 1)
		commandBuffer->GenerateMipmaps(*sharedImage, sharedImage->GetMipLevel());

	status = JobStatus::COMPLETE;
}
