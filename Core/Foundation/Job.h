#pragma once

namespace Core
{
	enum class JobType
	{
		GRAPHICS_PRIMARY, GRAPHICS_SECONDARY, COMPUTE, TRANSFER
	};

	enum class JobStatus
	{
		PENDING, PROGRESS, COMPLETE
	};

	class CommandBuffer;
	class Job
	{
	public:
		Job(JobType type) : type(type) {}
		virtual ~Job() = default;

		JobType type;
		JobStatus status = JobStatus::PENDING;
		Job* next = nullptr;
		condition_variable* completionWait;

		CommandBuffer* commandBuffer;

		virtual void Execute() = 0;
	};

	struct WorkQueue
	{
		Job* first = nullptr;
		Job* last = nullptr;

		size_t length = 0;
		void Add(const Job* job);
		Job* Pop();
		Job* GetNext();
		void Clear();

		bool Done();
	};
}

