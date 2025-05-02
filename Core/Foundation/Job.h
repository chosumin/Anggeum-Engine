#pragma once

namespace Core
{
	enum class JobStatus
	{
		PENDING, PROGRESS, COMPLETE
	};

	class Job
	{
	public:
		JobStatus status = JobStatus::PENDING;
		Job* next = nullptr;

		virtual void Execute(CommandBuffer& commandBuffer) = 0;
	};

	struct WorkQueue
	{
		Job* first = nullptr;
		Job* last = nullptr;

		size_t length;
		void Add(const Job* job);
		Job* GetNext();
		void Clear();

		bool Done();
	};
}

