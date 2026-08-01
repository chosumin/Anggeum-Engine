#include "stdafx.h"
#include "BarrierBatch2.h"
#include "CommandBuffer.h"
#include "Device.h"
#include "Buffer.h"
#include "Texture.h"
#include "Image.h"

Core::BarrierBatch2::BarrierBatch2(CommandBuffer& commandBuffer, Device& device, uint32_t queueFamilyIndex)
	: _commandBuffer(commandBuffer)
	, _device(device)
	, _queueFamilyIndex(queueFamilyIndex)
{
}

Core::BarrierBatch2& Core::BarrierBatch2::Buffer(Core::Buffer& buffer,
	VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
	VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
	VkBufferMemoryBarrier2 barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
	barrier.srcStageMask = srcStage;
	barrier.srcAccessMask = srcAccess;
	barrier.dstStageMask = dstStage;
	barrier.dstAccessMask = dstAccess;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer = buffer.GetBuffer();
	barrier.offset = 0;
	barrier.size = VK_WHOLE_SIZE;

	_bufferBarriers.push_back(barrier);

	return *this;
}

Core::BarrierBatch2& Core::BarrierBatch2::Image(Core::Texture& texture,
	VkImageLayout oldLayout, VkImageLayout newLayout,
	VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
	VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
	Core::Image& image = texture.GetImage();

	VkImageMemoryBarrier2 barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	barrier.srcStageMask = srcStage;
	barrier.srcAccessMask = srcAccess;
	barrier.dstStageMask = dstStage;
	barrier.dstAccessMask = dstAccess;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image.GetImage();
	barrier.subresourceRange.aspectMask = image.GetAspectFlags();
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = image.GetMipLevel();
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = image.GetLayer();

	_imageBarriers.push_back(barrier);

	return *this;
}

Core::BarrierBatch2& Core::BarrierBatch2::Image(VkImage image, VkImageAspectFlags aspect,
	VkImageLayout oldLayout, VkImageLayout newLayout,
	VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
	VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
	VkImageMemoryBarrier2 barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	barrier.srcStageMask = srcStage;
	barrier.srcAccessMask = srcAccess;
	barrier.dstStageMask = dstStage;
	barrier.dstAccessMask = dstAccess;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = aspect;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	_imageBarriers.push_back(barrier);

	return *this;
}

void Core::BarrierBatch2::Submit()
{
	if (Empty())
		return;

	for (auto& barrier : _bufferBarriers)
	{
		barrier.srcStageMask = SanitizeStageMask(barrier.srcStageMask);
		barrier.dstStageMask = SanitizeStageMask(barrier.dstStageMask);
	}
	for (auto& barrier : _imageBarriers)
	{
		barrier.srcStageMask = SanitizeStageMask(barrier.srcStageMask);
		barrier.dstStageMask = SanitizeStageMask(barrier.dstStageMask);
	}

	VkDependencyInfo dependencyInfo{};
	dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependencyInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(_bufferBarriers.size());
	dependencyInfo.pBufferMemoryBarriers = _bufferBarriers.data();
	dependencyInfo.imageMemoryBarrierCount = static_cast<uint32_t>(_imageBarriers.size());
	dependencyInfo.pImageMemoryBarriers = _imageBarriers.data();

	vkCmdPipelineBarrier2(_commandBuffer.GetHandle(), &dependencyInfo);

	// Reset so the batch can be reused for the next set of barriers.
	_bufferBarriers.clear();
	_imageBarriers.clear();
}

VkPipelineStageFlags2 Core::BarrierBatch2::SanitizeStageMask(VkPipelineStageFlags2 stageMask) const
{
	const auto& qfi = _device.GetQueueFamilyIndices();

	// Only the dedicated compute queue needs stage sanitizing. Graphics queue
	// supports all of the stages used here.
	if (!qfi.ComputeFamily.has_value() ||
		_queueFamilyIndex != qfi.ComputeFamily.value())
		return stageMask;

	const VkPipelineStageFlags2 graphicsOnly =
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
		VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
		VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
		VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
		VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
		VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT |
		VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
		VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT |
		VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT |
		VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT |
		VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT |
		VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;

	if (stageMask & graphicsOnly)
	{
		stageMask &= ~graphicsOnly;
		stageMask |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	}

	if (stageMask == 0)
		stageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;

	return stageMask;
}
