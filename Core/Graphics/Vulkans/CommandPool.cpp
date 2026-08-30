#include "stdafx.h"
#include "CommandPool.h"
#include "CommandBuffer.h"
#include "Graphics/SyncContext.h"

namespace Core
{
	namespace
	{
		uint32_t FamilyOf(Device& device, QueueType queue)
		{
			const auto& families = device.GetQueueFamilyIndices();
			switch (queue)
			{
			case QueueType::Compute:  return families.ComputeFamily.value();
			case QueueType::Transfer: return families.TransferFamily.value();
			case QueueType::Graphics: return families.GraphicsFamily.value();
			default:
				assert(false && "command pools need a concrete queue type");
				return families.GraphicsFamily.value();
			}
		}
	}

	CommandPool::CommandPool(Device& device, QueueType queue)
		:_device(device), _queueFamilyIndex(FamilyOf(device, queue))
	{
		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = _queueFamilyIndex;

		if (vkCreateCommandPool(device.GetDevice(), &poolInfo, nullptr, &_commandPool) != VK_SUCCESS)
			throw runtime_error("failed to create command pool!");
	}

	CommandPool::~CommandPool()
	{
		_primaryCommandBuffers.clear();
		_secondaryCommandBuffers.clear();

		vkDestroyCommandPool(_device.GetDevice(), _commandPool, nullptr);
	}

	CommandPool::CommandPool(Device& device, SyncContext& sync, QueueType queue)
		: CommandPool(device, queue)
	{
		_sync = &sync;
		_timelineQueue = queue;
	}

	CommandBuffer& CommandPool::RequestCommandBuffer(VkCommandBufferLevel level)
	{
		auto& buffers = level == VK_COMMAND_BUFFER_LEVEL_PRIMARY ?
			_primaryCommandBuffers : _secondaryCommandBuffers;

		// Recycling on GPU ground truth: a buffer is free once its timeline
		// passed its stamped submission value. The CACHED value on purpose -
		// workers request concurrently, and a per-frame-stale value only
		// delays recycling. Unbound pools (blocking one-shots, stamped 0
		// after their wait) reduce to "not checked out".
		const uint64_t completed = _sync != nullptr
			? _sync->GetCompletedValue(_timelineQueue) : 0;

		for (auto&& buffer : buffers)
		{
			if (!buffer->IsBusy(completed))
			{
				buffer->ResetCommandBuffer();
				buffer->MarkCheckedOut();
				return *buffer;
			}
		}

		buffers.emplace_back(make_unique<CommandBuffer>(_device, *this, level));
		buffers.back()->MarkCheckedOut();

		return *buffers.back();
	}

}
