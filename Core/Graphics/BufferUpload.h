#pragma once
#include "Foundation/Job.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/MemoryAllocator.h"
#include "Graphics/Vulkans/CommandBuffer.h"

namespace Core
{
	// Generic typed buffer fill, for uploads OUTSIDE the transfer pipeline:
	// frame-resource init (recorded into the frame's resource-init submission
	// during pass Setup - after the frame's transfer Flush) and legacy
	// immediate submits. Hence plain Job, not UploadJob, and dedicated
	// one-shot staging instead of a StagingRing span: the ring reclaims by
	// TRANSFER-timeline values, which these submissions never produce - an
	// unclosed span would block all reclamation behind it.
	template<typename T>
	class BufferUploadJob : public Job
	{
	public:
		BufferUploadJob(Device& device, Buffer& dstBuffer, vector<T>&& bufferData,
			VkDeviceSize dstOffset)
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
				MemoryType::DEDICATED_HOST);

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
