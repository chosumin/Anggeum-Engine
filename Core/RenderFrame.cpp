#include "stdafx.h"
//#include "RenderFrame.h"
//#include "CommandBuffer.h"
//
//namespace Core
//{
//	RenderFrame::RenderFrame(Device& device,
//		RenderTarget* renderTarget, size_t threadCount)
//		:_swapchainRenderTarget{ renderTarget },
//		_threadCount(threadCount)
//	{
//		for (auto i = 0; i < threadCount; ++i)
//		{
//			_descriptorPools.push_back(make_unique<unordered_map<size_t, DescriptorPool>>());
//			_descriptorSets.push_back(make_unique<unordered_map<size_t, DescriptorSet>>());
//		}
//	}
//
//	void RenderFrame::Reset()
//	{
//	}
//
//	void RenderFrame::UpdateRenderTarget(RenderTarget* renderTarget)
//	{
//		_swapchainRenderTarget = renderTarget;
//	}
//
//	vector<unique_ptr<CommandPool>>& RenderFrame::GetCommandPools(const Queue& queue, CommandBuffer::ResetMode resetMode)
//	{
//		// // O: 여기에 return 문을 삽입합니다.
//	}
//}