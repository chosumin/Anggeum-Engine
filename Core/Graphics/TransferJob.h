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
		VkBufferJob(Device& device, VkBufferUsageFlags usageFlag, Buffer** dstBuffer, vector<T> bufferData, bool empty = false)
			:Job(JobType::TRANSFER), _device(device), _destination(dstBuffer), _bufferData(bufferData), _usageFlag(usageFlag)
		{
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

			commandBuffer->CopyBuffer(*_stagingBuffer, *vertexBuffer, _dstOffset);

			*_destination = vertexBuffer;

			status = JobStatus::COMPLETE;
		}
	private:
		Device& _device;
		vector<T> _bufferData;
		VkDeviceSize _dstOffset;

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

	// Buffer Copy Job based on offset
	template<typename T>
	class VkBufferCopyJob : public Job
	{
	public:
		VkBufferCopyJob(Device& device, Buffer* dstBuffer, vector<T>&& bufferData, VkDeviceSize dstOffset)
			: Job(JobType::TRANSFER)
			, _device(device)
			, _destination(dstBuffer)
			, _bufferData(bufferData)
			, _dstOffset(dstOffset)
			, _stagingBuffer(nullptr)
		{
		}

		~VkBufferCopyJob()
		{
			delete _stagingBuffer;
		}

		void Execute() override
		{
			VkDeviceSize bufferSize = sizeof(T) * _bufferData.size();

			_stagingBuffer = new Core::Buffer(_device,
				bufferSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				MemoryType::STAGE);

			_stagingBuffer->CopyBuffer(_bufferData.data(), bufferSize);

			commandBuffer->CopyBuffer(*_stagingBuffer, *_destination, _dstOffset);

			status = JobStatus::COMPLETE;
		}

	private:
		Device& _device;
		vector<T> _bufferData;
		VkDeviceSize _dstOffset;

		Buffer* _destination;
		Buffer* _stagingBuffer;
	};
}

