#pragma once
#include "Foundation/Job.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/MemoryAllocator.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Image.h"

namespace Core
{
	class CommandBuffer;
	class Buffer;

	template<typename T>
	class VkBufferJob : public Job
	{
	public:
		VkBufferJob(Device& device, VkBufferUsageFlags usageFlag, Buffer** dstBuffer, vector<T> bufferData, bool isStorageBuffer = false) 
			:Job(JobType::TRANSFER), _device(device), _destination(dstBuffer), _bufferData(bufferData), _usageFlag(usageFlag)
		{
			if (isStorageBuffer)
			{
				_usageFlag |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			}
		}

		~VkBufferJob()
		{
			delete(_stagingBuffer);
		}

		void Execute() override
		{
			VkDeviceSize bufferSize = sizeof(_bufferData[0]) * _bufferData.size();

			auto allocatorManager = _device.GetMemoryAllocatorManager();

			_stagingBuffer = new Core::Buffer(_device,
				bufferSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				MemoryType::STAGE);

			_stagingBuffer->CopyBuffer(_bufferData.data(), bufferSize);

			auto vertexBuffer = new Core::Buffer(_device,
				bufferSize,
				VK_BUFFER_USAGE_TRANSFER_DST_BIT | _usageFlag,
				MemoryType::DEVICE_LOCAL);

			commandBuffer->CopyBuffer(*_stagingBuffer, *vertexBuffer);

			*_destination = vertexBuffer;

			status = JobStatus::COMPLETE;
		}
	private:
		Device& _device;
		vector<T> _bufferData;

		VkBufferUsageFlags _usageFlag;

		Buffer** _destination;
		Buffer* _stagingBuffer;
	};

	class Image;
	class VkImageJob : public Job
	{
	public:
		VkImageJob(Device& device, weak_ptr<Image> dstImage, string filePath);
		~VkImageJob();

		void Execute() override;
	private:
		Device& _device;
		string _filePath;

		weak_ptr<Image> _dstImage;
		Buffer* _stagingBuffer;
	};
}

