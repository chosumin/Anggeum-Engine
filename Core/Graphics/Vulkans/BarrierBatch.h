#pragma once
#include "Graphics/SyncContext.h"

namespace Core
{
	class Device;
	class Buffer;
	class Texture;
	class CommandBuffer;

	// Accumulates memory / buffer / image barriers and flushes them as a single
	// vkCmdPipelineBarrier. Create one via CommandBuffer::CreateBarrierBatch(),
	// chain the recording calls, then Submit().
	class BarrierBatch
	{
	public:
		BarrierBatch(CommandBuffer& commandBuffer, Device& device, uint32_t queueFamilyIndex);

		// Global memory barrier.
		BarrierBatch& Memory(
			VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
			VkAccessFlags srcAccess, VkAccessFlags dstAccess);

		// Buffer memory barrier. destQueue != None records a queue-ownership transfer.
		BarrierBatch& Buffer(
			Core::Buffer& buffer,
			VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
			VkAccessFlags srcAccess, VkAccessFlags dstAccess,
			QueueType destQueue = QueueType::None);

		// Image layout transition. Access masks and pipeline stages are inferred
		// from the layouts. destQueue != None records a queue-ownership transfer.
		BarrierBatch& Image(
			Core::Texture& texture,
			VkImageLayout oldLayout, VkImageLayout newLayout,
			QueueType destQueue = QueueType::None);

		// Records the accumulated barriers into the command buffer as one
		// vkCmdPipelineBarrier, then clears the batch so it can be reused.
		// No-op if nothing has been recorded.
		void Submit();

		bool Empty() const
		{
			return _memoryBarriers.empty() &&
				_bufferBarriers.empty() &&
				_imageBarriers.empty();
		}

	private:
		void GetAccessAndStageMask(VkImageLayout imageLayout,
			VkAccessFlags& outAccessFlags, VkPipelineStageFlags& outPipelineStageFlags) const;
		VkPipelineStageFlags SanitizeStageMask(VkPipelineStageFlags stageMask) const;
		void ResolveQueueOwnership(QueueType destQueue,
			uint32_t& outSrcFamily, uint32_t& outDstFamily) const;

	private:
		CommandBuffer& _commandBuffer;
		Device& _device;

		// Queue family the owning command buffer records for. Used to strip
		// pipeline stages the queue does not support and to resolve ownership
		// transfers.
		uint32_t _queueFamilyIndex;

		VkPipelineStageFlags _srcStageMask = 0;
		VkPipelineStageFlags _dstStageMask = 0;

		vector<VkMemoryBarrier> _memoryBarriers;
		vector<VkBufferMemoryBarrier> _bufferBarriers;
		vector<VkImageMemoryBarrier> _imageBarriers;
	};
}
