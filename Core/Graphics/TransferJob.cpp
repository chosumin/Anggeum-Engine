#include "stdafx.h"
#include "TransferJob.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/MemoryAllocator.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Image.h"

Core::VkBufferJob::VkBufferJob(Device& device, VkBufferUsageFlagBits usageFlag, 
	Buffer** dstBuffer, vector<uint8_t> vertexData)
	:Job(JobType::TRANSFER), _device(device), _destination(dstBuffer), _vertexData(vertexData),
	_usageFlag(usageFlag)
{
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

Core::VkImageJob::VkImageJob(Device& device, Image& dstImage, string filePath)
	:Job(JobType::TRANSFER), _device(device), _dstImage(dstImage), _filePath(filePath)
{
}

Core::VkImageJob::~VkImageJob()
{
	delete(_stagingBuffer);
}

void Core::VkImageJob::Execute()
{
	vector<uint8_t> imageData;
	_dstImage.Load(imageData);

	VkDeviceSize bufferSize = imageData.size();

	_stagingBuffer = new Core::Buffer(_device,
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
