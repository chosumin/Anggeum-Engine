#pragma once
#include "Foundation/Job.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/MemoryAllocator.h"
#include "Graphics/Vulkans/CommandBuffer.h"

namespace Core
{
	// Generic typed buffer fill, for uploads outside the geometry/texture
	// domains.
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
