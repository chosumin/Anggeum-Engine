#pragma once

namespace Core
{
	class Device;
	class Buffer;
	class Texture;
	class CommandBuffer;

	// Accumulates buffer / image barriers and flushes them as a single PipelineBarrier.
	class BarrierBatch
	{
	public:
		BarrierBatch(CommandBuffer& commandBuffer, Device& device, uint32_t queueFamilyIndex);

		BarrierBatch& Buffer(Core::Buffer& buffer,
			VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
			VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

		BarrierBatch& Image(Core::Texture& texture,
			VkImageLayout oldLayout, VkImageLayout newLayout,
			VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
			VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

		// Convenience layout transition: stages and accesses are inferred from
		// the layouts (conservative upper bounds).
		BarrierBatch& Image(Core::Texture& texture,
			VkImageLayout oldLayout, VkImageLayout newLayout);

		// Raw-handle variant for images without an engine wrapper (swapchain).
		BarrierBatch& Image(VkImage image, VkImageAspectFlags aspect,
			VkImageLayout oldLayout, VkImageLayout newLayout,
			VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
			VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

		// Records the accumulated barriers as one barrier call, then
		// clears the batch so it can be reused. No-op if nothing has been recorded.
		void Submit();

		bool Empty() const
		{
			return _bufferBarriers.empty() && _imageBarriers.empty();
		}

	private:
		static void GetAccessAndStageMask(VkImageLayout imageLayout,
			VkAccessFlags2& outAccess, VkPipelineStageFlags2& outStage);
		VkPipelineStageFlags2 SanitizeStageMask(VkPipelineStageFlags2 stageMask) const;
		VkAccessFlags2 SanitizeAccessMask(VkAccessFlags2 accessMask) const;

	private:
		CommandBuffer& _commandBuffer;
		Device& _device;

		// Queue family the owning command buffer records for. Used to strip
		// pipeline stages the queue does not support.
		uint32_t _queueFamilyIndex;

		vector<VkBufferMemoryBarrier2> _bufferBarriers;
		vector<VkImageMemoryBarrier2> _imageBarriers;
	};
}
