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
		// The job creates the destination buffer and hands ownership to `dstBuffer`,
		// which must outlive the job.
		VkBufferJob(Device& device, VkBufferUsageFlags usageFlag, unique_ptr<Buffer>& dstBuffer, vector<T> bufferData, bool empty = false)
			:Job(JobType::TRANSFER), _device(device), _destination(dstBuffer), _bufferData(bufferData), _usageFlag(usageFlag), _dstOffset(0)
		{
		}

		void Execute() override
		{
			VkDeviceSize bufferSize = sizeof(_bufferData[0]) * _bufferData.size();

			_stagingBuffer = make_unique<Core::Buffer>(_device,
				bufferSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				MemoryType::STAGE);

			_stagingBuffer->CopyBuffer(_bufferData.data(), bufferSize);

			auto vertexBuffer = make_unique<Core::Buffer>(_device,
				bufferSize,
				VK_BUFFER_USAGE_TRANSFER_DST_BIT | _usageFlag,
				MemoryType::DEVICE_LOCAL);

			commandBuffer->CopyBuffer(*_stagingBuffer, *vertexBuffer, _dstOffset);

			_destination = std::move(vertexBuffer);

			status = JobStatus::COMPLETE;
		}
	private:
		Device& _device;
		vector<T> _bufferData;
		VkDeviceSize _dstOffset;

		VkBufferUsageFlags _usageFlag;

		unique_ptr<Buffer>& _destination;
		unique_ptr<Buffer> _stagingBuffer;
	};

	class Image;
	class Texture;
	class VkImageJob : public Job
	{
	public:
		// Takes a main-thread resolved Texture&; the job records on a worker thread,
		// so it must not resolve a handle through the (non-thread-safe) pool itself.
		VkImageJob(Device& device, Texture& dstTexture, string filePath);
		~VkImageJob();

		void Execute() override;
	private:
		Device& _device;
		string _filePath;

		Texture& _dstTexture;
		unique_ptr<Buffer> _stagingBuffer;
	};

	// Buffer Copy Job based on offset
	template<typename T>
	class VkBufferCopyJob : public Job
	{
	public:
		// Copies into an existing buffer owned by the caller, which must outlive
		// the job.
		VkBufferCopyJob(Device& device, Buffer& dstBuffer, vector<T>&& bufferData, VkDeviceSize dstOffset)
			: Job(JobType::TRANSFER)
			, _device(device)
			, _destination(dstBuffer)
			, _bufferData(bufferData)
			, _dstOffset(dstOffset)
		{
		}

		void Execute() override
		{
			VkDeviceSize bufferSize = sizeof(T) * _bufferData.size();

			_stagingBuffer = make_unique<Core::Buffer>(_device,
				bufferSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				MemoryType::STAGE);

			_stagingBuffer->CopyBuffer(_bufferData.data(), bufferSize);

			commandBuffer->CopyBuffer(*_stagingBuffer, _destination, _dstOffset);

			status = JobStatus::COMPLETE;
		}

	private:
		Device& _device;
		vector<T> _bufferData;
		VkDeviceSize _dstOffset;

		Buffer& _destination;
		unique_ptr<Buffer> _stagingBuffer;
	};
}

