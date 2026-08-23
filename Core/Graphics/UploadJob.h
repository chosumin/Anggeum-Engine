#pragma once
#include "Foundation/Job.h"

namespace Core
{
	class UploadJob : public Job
	{
	public:
		UploadJob() : Job(JobType::TRANSFER) {}

		uint64_t stagingSpanId = UINT64_MAX;
	};
}
