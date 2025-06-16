#include "stdafx.h"
#include "TransferJob.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/MemoryAllocator.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Image.h"

Core::VkBufferJob::VkBufferJob(Device& device, VkBufferUsageFlags usageFlag,
	Buffer** dstBuffer, vector<uint8_t> vertexData, bool isStorageBuffer)
	:Job(JobType::TRANSFER), _device(device), _destination(dstBuffer), _vertexData(vertexData),
	_usageFlag(usageFlag)
{
	if (isStorageBuffer)
	{
		_usageFlag |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	}
}

Core::VkBufferJob::~VkBufferJob()
{
	delete(_stagingBuffer);
}

void Core::VkBufferJob::Execute()
{
	VkDeviceSize bufferSize = sizeof(_vertexData[0]) * _vertexData.size();

	auto allocatorManager = _device.GetMemoryAllocatorManager();
	
	_stagingBuffer = new Core::Buffer(_device,
		bufferSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		MemoryType::STAGE);

	_stagingBuffer->CopyBuffer(_vertexData.data(), bufferSize);

	auto vertexBuffer = new Core::Buffer(_device,
		bufferSize,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | _usageFlag,
		MemoryType::DEVICE_LOCAL);

	commandBuffer->CopyBuffer(*_stagingBuffer, *vertexBuffer);

	*_destination = vertexBuffer;

	status = JobStatus::COMPLETE;
}

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
