#include "stdafx.h"
#include "GeometryUpload.h"
#include "StagingRing.h"
#include "Graphics/Vulkans/Buffer.h"
#include "Graphics/Vulkans/CommandBuffer.h"
#include "Graphics/Vulkans/MemoryAllocator.h"

using namespace Core;

GeometryUploadJob::GeometryUploadJob(Device& device, GeometryCopyBatch&& batch,
	GeometryBounds* boundsResult, StagingRing* stagingRing)
	: Job(JobType::TRANSFER)
	, _device(device)
	, _batch(std::move(batch))
	, _boundsResult(boundsResult)
	, _stagingRing(stagingRing)
{
	// Handles resolve here, on the constructing (main) thread.
	_destinations.reserve(_batch.copies.size());
	for (auto& copy : _batch.copies)
		_destinations.push_back(&copy.destination.Get());
}

GeometryUploadJob::~GeometryUploadJob() = default;

void GeometryUploadJob::Execute()
{
	// Runs here rather than on the loading thread: scanning every vertex is the
	// expensive part of a mesh upload, and the data is already resident.
	if (_boundsResult != nullptr)
	{
		for (auto& copy : _batch.copies)
		{
			if (copy.boundsStride == 0)
				continue;

			ComputeGeometryBounds(copy.data.data(), copy.data.size(),
				copy.boundsStride, *_boundsResult);
		}
	}

	VkDeviceSize totalSize = 0;
	for (auto& copy : _batch.copies)
		totalSize += copy.data.size();

	StagingRing::Span span = _stagingRing != nullptr
		? _stagingRing->Acquire(totalSize) : StagingRing::Span{};

	Buffer* source = nullptr;
	VkDeviceSize base = 0;
	VkDeviceSize srcOffset = 0;

	if (span.IsValid())
	{
		// Pack straight into the persistently mapped ring span.
		for (auto& copy : _batch.copies)
		{
			memcpy(span.mapped + srcOffset, copy.data.data(), copy.data.size());
			srcOffset += copy.data.size();
		}
		source = span.buffer;
		base = span.offset;
	}
	else
	{
		// Ring full (or absent): dedicated staging, freed with the job.
		vector<uint8_t> packed(totalSize);
		for (auto& copy : _batch.copies)
		{
			memcpy(packed.data() + srcOffset, copy.data.data(), copy.data.size());
			srcOffset += copy.data.size();
		}

		_stagingBuffer = make_unique<Core::Buffer>(_device,
			totalSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			MemoryType::STAGE);
		_stagingBuffer->CopyBuffer(packed.data(), totalSize);
		source = _stagingBuffer.get();
	}

	srcOffset = 0;
	for (size_t i = 0; i < _batch.copies.size(); ++i)
	{
		auto& copy = _batch.copies[i];
		commandBuffer->CopyBuffer(*source, *_destinations[i],
			copy.offset, base + srcOffset, copy.data.size());
		srcOffset += copy.data.size();
	}

	status = JobStatus::COMPLETE;
}
