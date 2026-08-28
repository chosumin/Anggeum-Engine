#pragma once
#include "Foundation/Job.h"

namespace Core
{
	class StagingRing;

	class UploadJob : public Job
	{
	public:
		UploadJob() : Job(JobType::TRANSFER) {}

		StagingRing* stagingRing = nullptr;
		uint64_t stagingSpanId = UINT64_MAX;
	};
}
