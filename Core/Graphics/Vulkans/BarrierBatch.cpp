#include "stdafx.h"
#include "BarrierBatch.h"
#include "CommandBuffer.h"
#include "Device.h"
#include "Buffer.h"
#include "Texture.h"
#include "Image.h"

Core::BarrierBatch::BarrierBatch(CommandBuffer& commandBuffer, Device& device, uint32_t queueFamilyIndex)
	: _commandBuffer(commandBuffer)
	, _device(device)
	, _queueFamilyIndex(queueFamilyIndex)
{
}

Core::BarrierBatch& Core::BarrierBatch::Buffer(
	Core::Buffer& buffer,
	VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
	VkAccessFlags srcAccess, VkAccessFlags dstAccess,
	QueueType destQueue)
{
	VkBufferMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	barrier.srcAccessMask = srcAccess;
	barrier.dstAccessMask = dstAccess;

	ResolveQueueOwnership(destQueue, barrier.srcQueueFamilyIndex, barrier.dstQueueFamilyIndex);

	barrier.buffer = buffer.GetBuffer();
	barrier.offset = 0;
	barrier.size = VK_WHOLE_SIZE;

	_bufferBarriers.push_back(barrier);

	_srcStageMask |= srcStage;
	_dstStageMask |= dstStage;

	return *this;
}

Core::BarrierBatch& Core::BarrierBatch::Image(
	Core::Texture& texture,
	VkImageLayout oldLayout, VkImageLayout newLayout,
	QueueType destQueue)
{
	Core::Image& image = texture.GetImage();

	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;

	ResolveQueueOwnership(destQueue, barrier.srcQueueFamilyIndex, barrier.dstQueueFamilyIndex);

	barrier.image = image.GetImage();
	barrier.subresourceRange.aspectMask = image.GetAspectFlags();
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = image.GetMipLevel();
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = image.GetLayer();

	VkPipelineStageFlags sourceStage;
	VkPipelineStageFlags destinationStage;

	// Access masks and pipeline stages are inferred from the layouts.
	GetAccessAndStageMask(oldLayout, barrier.srcAccessMask, sourceStage);
	GetAccessAndStageMask(newLayout, barrier.dstAccessMask, destinationStage);

	_imageBarriers.push_back(barrier);

	_srcStageMask |= sourceStage;
	_dstStageMask |= destinationStage;

	return *this;
}

void Core::BarrierBatch::Submit()
{
	if (Empty())
		return;

	vkCmdPipelineBarrier(
		_commandBuffer.GetHandle(),
		SanitizeStageMask(_srcStageMask),
		SanitizeStageMask(_dstStageMask),
		0,
		0, nullptr,
		static_cast<uint32_t>(_bufferBarriers.size()), _bufferBarriers.data(),
		static_cast<uint32_t>(_imageBarriers.size()), _imageBarriers.data());

	// Reset so the batch can be reused for the next set of barriers.
	_bufferBarriers.clear();
	_imageBarriers.clear();
	_srcStageMask = 0;
	_dstStageMask = 0;
}

void Core::BarrierBatch::ResolveQueueOwnership(QueueType destQueue,
	uint32_t& outSrcFamily, uint32_t& outDstFamily) const
{
	if (destQueue == QueueType::None)
	{
		outSrcFamily = VK_QUEUE_FAMILY_IGNORED;
		outDstFamily = VK_QUEUE_FAMILY_IGNORED;
		return;
	}

	const auto& qfi = _device.GetQueueFamilyIndices();

	if (destQueue == QueueType::Graphics)
	{
		outSrcFamily = qfi.ComputeFamily.value();
		outDstFamily = qfi.GraphicsFamily.value();
	}
	else
	{
		outSrcFamily = qfi.GraphicsFamily.value();
		outDstFamily = qfi.ComputeFamily.value();
	}
}

void Core::BarrierBatch::GetAccessAndStageMask(VkImageLayout imageLayout,
	VkAccessFlags& outAccessFlags, VkPipelineStageFlags& outPipelineStageFlags) const
{
	switch (imageLayout)
	{
	case VK_IMAGE_LAYOUT_UNDEFINED:
		outAccessFlags = 0;
		outPipelineStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		outAccessFlags = VK_ACCESS_TRANSFER_WRITE_BIT;
		outPipelineStageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
		break;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		outAccessFlags = VK_ACCESS_SHADER_READ_BIT;
		outPipelineStageFlags = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		break;
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		outAccessFlags = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		outPipelineStageFlags = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		break;
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		outAccessFlags = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		outPipelineStageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		outAccessFlags = VK_ACCESS_TRANSFER_READ_BIT;
		outPipelineStageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
		break;
	case VK_IMAGE_LAYOUT_GENERAL:
		outAccessFlags = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		outPipelineStageFlags = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		break;
	default:
		throw invalid_argument("unsupported layout transition!");
		break;
	}
}

VkPipelineStageFlags Core::BarrierBatch::SanitizeStageMask(VkPipelineStageFlags stageMask) const
{
	const auto& qfi = _device.GetQueueFamilyIndices();

	// Only the dedicated compute queue needs stage sanitizing. Graphics queue
	// supports all of the stages used here.
	if (!qfi.ComputeFamily.has_value() ||
		_queueFamilyIndex != qfi.ComputeFamily.value())
		return stageMask;

	// Graphics-only pipeline stages are invalid on a compute queue. Replace
	// them with the closest compute-compatible equivalent so the queue
	// ownership barriers stay spec-compliant.
	const VkPipelineStageFlags graphicsOnly =
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
		VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
		VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
		VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
		VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT |
		VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
		VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;

	if (stageMask & graphicsOnly)
	{
		stageMask &= ~graphicsOnly;
		stageMask |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	}

	if (stageMask == 0)
		stageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

	return stageMask;
}
