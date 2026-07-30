#pragma once

namespace Core
{
	class Device;
	class Buffer;
	class Texture;
	class CommandBuffer;

	class BarrierBatch2
	{
	public:
		BarrierBatch2(CommandBuffer& commandBuffer, Device& device, uint32_t queueFamilyIndex);

		BarrierBatch2& Buffer(Core::Buffer& buffer,
			VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
			VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

		BarrierBatch2& Image(Core::Texture& texture,
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
		VkPipelineStageFlags2 SanitizeStageMask(VkPipelineStageFlags2 stageMask) const;

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
