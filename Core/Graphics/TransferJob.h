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

	// One staging->device copy inside a VkBufferCopyBatchJob.
	struct BufferCopyRegion
	{
		Buffer* destination;   // caller-owned, must outlive the job
		vector<uint8_t> data;
		VkDeviceSize dstOffset;
	};

	// Copies many regions into their destinations within a single transfer job, 
	// so a mesh upload is one worker-thread task.
	class VkBufferCopyBatchJob : public Job
	{
	public:
		VkBufferCopyBatchJob(Device& device, vector<BufferCopyRegion>&& regions)
			: Job(JobType::TRANSFER)
			, _device(device)
			, _regions(std::move(regions))
		{
		}

		void Execute() override
		{
			// Pack every region into one staging buffer and copy each out of its
			// sub-range, so a mesh upload needs a single staging allocation.
			VkDeviceSize totalSize = 0;
			for (auto& region : _regions)
				totalSize += region.data.size();

			_stagingBuffer = make_unique<Core::Buffer>(_device,
				totalSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				MemoryType::STAGE);

			void* mapped = nullptr;
			_stagingBuffer->Map(&mapped);
			auto* base = static_cast<uint8_t*>(mapped);

			VkDeviceSize srcOffset = 0;
			for (auto& region : _regions)
			{
				VkDeviceSize size = region.data.size();

				memcpy(base + srcOffset, region.data.data(), size);
				commandBuffer->CopyBuffer(*_stagingBuffer, *region.destination,
					region.dstOffset, srcOffset, size);

				srcOffset += size;
			}

			_stagingBuffer->Unmap();

			status = JobStatus::COMPLETE;
		}

	private:
		Device& _device;
		vector<BufferCopyRegion> _regions;
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

