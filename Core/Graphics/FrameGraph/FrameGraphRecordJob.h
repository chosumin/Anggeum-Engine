#pragma once
#include "Foundation/Job.h"
#include "Graphics/SyncContext.h"

namespace Core
{
	class FrameGraph;
	class FrameGraphRecordJob : public Job
	{
	public:
		FrameGraphRecordJob(FrameGraph& graph, uint32_t passIndex,
			uint32_t timerPassIndex, QueueType queue)
			: Job(queue == QueueType::Compute ? JobType::COMPUTE : JobType::GRAPHICS_PRIMARY)
			, _graph(graph)
			, _passIndex(passIndex)
			, _timerPassIndex(timerPassIndex)
		{
		}

		void Execute() override;

	private:
		FrameGraph& _graph;
		uint32_t _passIndex;
		uint32_t _timerPassIndex;
	};
}
