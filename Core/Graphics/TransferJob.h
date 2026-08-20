#pragma once
#include "Foundation/Job.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/MemoryAllocator.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/Image.h"
#include "Graphics/GeometryUpload.h"
#include "Graphics/StagingRing.h"

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
		// With a ring, staging comes from it; oversized loads (and callers without
		// one) fall back to a dedicated one-shot staging buffer.
		VkImageJob(Device& device, Texture& dstTexture, string filePath,
			StagingRing* stagingRing = nullptr);
		~VkImageJob();

		void Execute() override;
	private:
		Device& _device;
		string _filePath;

		Texture& _dstTexture;
		StagingRing* _stagingRing;
		unique_ptr<Buffer> _stagingBuffer;
	};

	// One staging->device copy inside a VkBufferCopyBatchJob.
	struct BufferCopyRegion
	{
		Buffer* destination;   // caller-owned, must outlive the job
		vector<uint8_t> data;
		VkDeviceSize dstOffset;
	};

	// Bounds to compute from one of the job's regions, while its data is already in
	// hand on the worker thread. `result` is owned by the caller and read only after
	// the job completes.
	struct BoundsTask
	{
		size_t regionIndex;
		uint32_t stride;
		GeometryBounds* result;
	};

	// Copies many regions into their destinations within a single transfer job,
	// so a mesh upload is one worker-thread task.
	class VkBufferCopyBatchJob : public Job
	{
	public:
		VkBufferCopyBatchJob(Device& device, vector<BufferCopyRegion>&& regions,
			vector<BoundsTask>&& boundsTasks = {}, StagingRing* stagingRing = nullptr)
			: Job(JobType::TRANSFER)
			, _device(device)
			, _regions(std::move(regions))
			, _boundsTasks(std::move(boundsTasks))
			, _stagingRing(stagingRing)
		{
		}

		void Execute() override
		{
			// Runs here rather than on the loading thread: scanning every vertex is the
			// expensive part of a mesh upload, and the data is already resident.
			for (auto& task : _boundsTasks)
			{
				auto& region = _regions[task.regionIndex];
				ComputeGeometryBounds(region.data.data(), region.data.size(),
					task.stride, *task.result);
			}

			// Pack every region into one staging span and copy each out of its
			// sub-range, so a mesh upload needs a single staging allocation.
			VkDeviceSize totalSize = 0;
			for (auto& region : _regions)
				totalSize += region.data.size();

			StagingRing::Span span = _stagingRing != nullptr
				? _stagingRing->Acquire(totalSize) : StagingRing::Span{};

			Buffer* source = nullptr;
			VkDeviceSize base = 0;
			VkDeviceSize srcOffset = 0;

			if (span.IsValid())
			{
				// Pack straight into the persistently mapped ring span.
				for (auto& region : _regions)
				{
					memcpy(span.mapped + srcOffset, region.data.data(), region.data.size());
					srcOffset += region.data.size();
				}
				source = span.buffer;
				base = span.offset;
			}
			else
			{
				// Ring full (or absent): dedicated staging, freed with the job.
				vector<uint8_t> packed(totalSize);
				for (auto& region : _regions)
				{
					memcpy(packed.data() + srcOffset, region.data.data(), region.data.size());
					srcOffset += region.data.size();
				}

				_stagingBuffer = make_unique<Core::Buffer>(_device,
					totalSize,
					VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
					MemoryType::STAGE);
				_stagingBuffer->CopyBuffer(packed.data(), totalSize);
				source = _stagingBuffer.get();
			}

			srcOffset = 0;
			for (auto& region : _regions)
			{
				commandBuffer->CopyBuffer(*source, *region.destination,
					region.dstOffset, base + srcOffset, region.data.size());
				srcOffset += region.data.size();
			}

			status = JobStatus::COMPLETE;
		}

	private:
		Device& _device;
		vector<BufferCopyRegion> _regions;
		vector<BoundsTask> _boundsTasks;
		StagingRing* _stagingRing;
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
			, _bufferData(std::move(bufferData))
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

