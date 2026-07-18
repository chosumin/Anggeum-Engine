#include "stdafx.h"
#include "CommandBuffer.h"
#include "Pipeline.h"
#include "SwapChain.h"
#include "Shader.h"
#include "Buffer.h"
#include "Texture.h"
#include "RenderPass.h"
#include "Framebuffer.h"
#include "Image.h"
#include "BindlessTextureManager.h"
#include "DescriptorPool.h"
#include "Graphics/RenderContext.h"
#include "Graphics/Material.h"
#include "Graphics/RenderFrame.h"
#include "Foundation/Job.h"

Core::CommandBuffer::CommandBuffer(Device& device, CommandPool& commandPool, VkCommandBufferLevel level)
	:_device(device), _level(level)
{
	_frame = -MAX_FRAMES_IN_FLIGHT;
	_queueFamilyIndex = commandPool.GetQueueFamilyIndex();

	VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool.GetHandle();
    allocInfo.level = level;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(_device.GetDevice(), &allocInfo, &_commandBuffer) != VK_SUCCESS)
        throw runtime_error("failed to allocate command buffers!");
}

void Core::CommandBuffer::ResetCommandBuffer()
{
    _frame = Core::FrameCounter::GetFrameNumber();

    vkResetCommandBuffer(_commandBuffer, 0);
}

void Core::CommandBuffer::BeginCommandBuffer(VkCommandBufferUsageFlags flags, const RenderPass* renderPass, const Framebuffer* framebuffer, uint32_t subpassIndex, uint32_t imageIndex)
{
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = flags;

	VkCommandBufferInheritanceInfo inheritanceInfo = {};
	if (_level == VK_COMMAND_BUFFER_LEVEL_SECONDARY)
	{
		inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;

		inheritanceInfo.renderPass = renderPass != nullptr ?
			renderPass->GetHandle() : VK_NULL_HANDLE;
		inheritanceInfo.framebuffer = framebuffer != nullptr ?
			framebuffer->GetHandle() : VK_NULL_HANDLE;
		inheritanceInfo.subpass = subpassIndex;
		inheritanceInfo.occlusionQueryEnable = VK_FALSE;
		inheritanceInfo.queryFlags = 0;
		inheritanceInfo.pipelineStatistics = 0;

		beginInfo.pInheritanceInfo = &inheritanceInfo;
	}

	auto result = vkBeginCommandBuffer(_commandBuffer, &beginInfo);
	if (result != VK_SUCCESS)
		throw std::runtime_error("failed to begin recording command buffer!");
}

void Core::CommandBuffer::BeginCommandBuffer(bool isSingleTime)
{
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	if (isSingleTime)
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	auto result = vkBeginCommandBuffer(_commandBuffer, &beginInfo);
	if (result != VK_SUCCESS)
		throw std::runtime_error("failed to begin recording command buffer!");
}

void Core::CommandBuffer::ExecuteCommands(vector<CommandBuffer*>& secondaryCommandBuffers)
{
    vector<VkCommandBuffer> secondaries(secondaryCommandBuffers.size(), VK_NULL_HANDLE);
    
    transform(
        secondaryCommandBuffers.begin(), secondaryCommandBuffers.end(),
        secondaries.begin(),
        [](const CommandBuffer* command) { return command->GetHandle(); });
    
    vkCmdExecuteCommands(_commandBuffer, 
        static_cast<uint32_t>(secondaries.size()),
        secondaries.data());
}

void Core::CommandBuffer::BeginRenderPass(VkRenderPassBeginInfo renderPassInfo)
{
    vkCmdBeginRenderPass(_commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void Core::CommandBuffer::BindPipeline(const Pipeline* pipeline)
{
    vkCmdBindPipeline(_commandBuffer, pipeline->GetPipelineBindPoint(), pipeline->GetPipeline());
}

void Core::CommandBuffer::SetViewportAndScissor(VkExtent2D extent)
{
    VkViewport viewport;
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    vkCmdSetViewport(_commandBuffer, 0, 1, &viewport);

    VkRect2D scissor;
    scissor.offset = { 0, 0 };
    scissor.extent = extent;

    vkCmdSetScissor(_commandBuffer, 0, 1, &scissor);
}

void Core::CommandBuffer::BindDescriptorSets(VkPipelineBindPoint pipelineBindPoint,
	Shader& shader,
	const vector<DescriptorSetResources*>& resourcesList)
{
	auto pipelineLayout = shader.GetPipelineLayout();

	for (auto* res : resourcesList)
	{
		if (!res || res->descriptorSet == VK_NULL_HANDLE)
			continue;

		vkCmdBindDescriptorSets(
			_commandBuffer, pipelineBindPoint,
			pipelineLayout, res->setIndex, 1,
			&res->descriptorSet, 0, nullptr);
	}
}

void Core::CommandBuffer::PushConstants(Material& material, uint32_t index)
{
    auto& shader = material.GetShader();

    auto pushConstants = material.GetPushConstantsData();

    if (pushConstants->empty())
		return;

	vkCmdPushConstants(_commandBuffer, shader.GetPipelineLayout(),
        shader.GetPushConstantsShaderStage(index),
		shader.GetPushConstantsOffset(index),
		static_cast<uint32_t>(pushConstants->size()),
		pushConstants->data());

	material.ClearPushConstantsCache();
}

void Core::CommandBuffer::PushConstants(Shader& shader, uint index, const void* data)
{
	auto& pushConstantRanges = shader.GetPushConstantRanges();

    vkCmdPushConstants(
        _commandBuffer,
        shader.GetPipelineLayout(),
        pushConstantRanges[index].stageFlags,
        pushConstantRanges[index].offset,
        pushConstantRanges[index].size,
        data);
}

void Core::CommandBuffer::BindVertexBuffers(Buffer& buffer, uint32_t binding)
{
    VkBuffer vertexBuffers[] = { buffer.GetBuffer() };
    VkDeviceSize offsets[] = { 0 };

    vkCmdBindVertexBuffers(_commandBuffer, binding, 1, vertexBuffers, offsets);
}

void Core::CommandBuffer::BindVertexBuffers(vector<Buffer*> buffers, uint32_t binding)
{
    vector<VkBuffer> vertexBuffers(buffers.size());
    transform(buffers.begin(), buffers.end(), vertexBuffers.begin(),
        [](Buffer* buf)
    {
        return buf->GetBuffer();
    });

	vector<VkDeviceSize> offsets(buffers.size(), 0);

    vkCmdBindVertexBuffers(_commandBuffer, binding, static_cast<uint32_t>(buffers.size()), vertexBuffers.data(), offsets.data());
}

void Core::CommandBuffer::BindIndexBuffer(Buffer& buffer, VkIndexType indexType)
{
    vkCmdBindIndexBuffer(_commandBuffer, buffer.GetBuffer(), 0, indexType);
}

void Core::CommandBuffer::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstInstance)
{
    vkCmdDrawIndexed(_commandBuffer, indexCount, instanceCount, 0, 0, firstInstance);
}

void Core::CommandBuffer::Draw(uint32_t vertexCount, uint32_t instanceCount)
{
	vkCmdDraw(_commandBuffer, vertexCount, instanceCount, 0, 0);
}

void Core::CommandBuffer::Dispatch(uint32_t x, uint32_t y, uint32_t z)
{
    vkCmdDispatch(_commandBuffer, x, y, z);
}

void Core::CommandBuffer::CopyBuffer(Buffer& srcBuffer, Buffer& dstBuffer, VkDeviceSize dstOffset)
{
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = srcBuffer.GetSize();

    vkCmdCopyBuffer(_commandBuffer, srcBuffer.GetBuffer(), dstBuffer.GetBuffer(),
        1, &copyRegion);
}

void Core::CommandBuffer::CopyImage(Image& srcImage, Image& dstImage, 
    uint32_t srcMipLevel, uint32_t srcLayer, uint32_t dstMipLevel, uint32_t dstLayer)
{
    VkImageCopy copyRegion{};

    copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.srcSubresource.baseArrayLayer = srcLayer;
    copyRegion.srcSubresource.mipLevel = srcMipLevel;
    copyRegion.srcSubresource.layerCount = 1;
    copyRegion.srcOffset = { 0, 0, 0 };

    copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.dstSubresource.baseArrayLayer = dstLayer;
    copyRegion.dstSubresource.mipLevel = dstMipLevel;
    copyRegion.dstSubresource.layerCount = 1;
    copyRegion.dstOffset = { 0, 0, 0 };

    auto extent = srcImage.GetExtent();

    VkExtent2D mipExtent;
    mipExtent.width = static_cast<uint32_t>(extent.width * pow(0.5f, dstMipLevel));
    mipExtent.height = static_cast<uint32_t>(extent.height * pow(0.5f, dstMipLevel));

    copyRegion.extent.width = static_cast<uint32_t>(mipExtent.width);
    copyRegion.extent.height = static_cast<uint32_t>(mipExtent.height);
    copyRegion.extent.depth = extent.depth;

    vkCmdCopyImage(
        _commandBuffer,
        srcImage.GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dstImage.GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copyRegion);
}

void Core::CommandBuffer::CopyBufferToImage(Buffer& buffer, Image& image, uint32_t width, uint32_t height)
{
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = image.GetLayer();

    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, 1 };

    vkCmdCopyBufferToImage(
        _commandBuffer,
        buffer.GetBuffer(),
        image.GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );
}

void Core::CommandBuffer::TransitionImageLayout(Image& image, VkImageLayout oldLayout, VkImageLayout newLayout, QueueType destQueue)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    
    if (destQueue == QueueType::None)
    {
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    }
    else
    {
        const auto& qfi = _device.GetQueueFamilyIndices();

        if (destQueue == QueueType::Graphics)
        {
            barrier.srcQueueFamilyIndex = qfi.ComputeFamily.value();
            barrier.dstQueueFamilyIndex = qfi.GraphicsFamily.value();
        }
        else
        {
            barrier.srcQueueFamilyIndex = qfi.GraphicsFamily.value();
            barrier.dstQueueFamilyIndex = qfi.ComputeFamily.value();
        }
    }

    barrier.image = image.GetImage();
    barrier.subresourceRange.aspectMask = image.GetAspectFlags();
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = image.GetMipLevel();
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = image.GetLayer();

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

	//tranfer writes that don't need to wait on anything.
	GetAccessAndStageMask(oldLayout, barrier.srcAccessMask, sourceStage);
	GetAccessAndStageMask(newLayout, barrier.dstAccessMask, destinationStage);

	sourceStage = SanitizeStageMask(sourceStage);
	destinationStage = SanitizeStageMask(destinationStage);

	vkCmdPipelineBarrier(
		_commandBuffer,
		sourceStage, destinationStage,
		0,
		0, nullptr,
		0, nullptr,
		1, &barrier
	);
}

void Core::CommandBuffer::GenerateMipmaps(Image& image, uint32_t mipLevels)
{
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(
        _device.GetPhysicalDevice(), image.GetFormat(), &formatProperties);

    if (!(formatProperties.optimalTilingFeatures &
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
        throw runtime_error("texture image format doesn't support linear blitting!");

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image.GetImage();
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = image.GetLayer();
    barrier.subresourceRange.levelCount = 1;

    auto extent = image.GetExtent();

    int32_t mipWidth = extent.width;
    int32_t mipHeight = extent.height;

    for (uint32_t i = 1; i < mipLevels; ++i)
    {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        //Source image layout switches to source read.
        vkCmdPipelineBarrier(_commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        //hack : hardcoded VK_IMAGE_ASPECT_COLOR_BIT
        VkImageBlit blit{};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = image.GetLayer();
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = image.GetLayer();

        //both the src and dst are the same image, because of blitting between different levels of the same image.
        vkCmdBlitImage(_commandBuffer,
            image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image.GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        //Source image switches back to shader read only.
        vkCmdPipelineBarrier(_commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        if (mipWidth > 1)
            mipWidth /= 2;
        if (mipHeight > 1)
            mipHeight /= 2;
    }

    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(_commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        0, nullptr,
        0, nullptr,
        1, &barrier);
}

void Core::CommandBuffer::EndRenderPass()
{
    vkCmdEndRenderPass(_commandBuffer);
}

void Core::CommandBuffer::EndCommandBuffer()
{
    if (vkEndCommandBuffer(_commandBuffer) != VK_SUCCESS)
        throw std::runtime_error("failed to record command buffer!");
}

bool Core::CommandBuffer::IsBusy()
{
    return _frame + 1 >= Core::FrameCounter::GetFrameNumber();
}

void Core::CommandBuffer::ImmediateSubmit(Core::Device& device, Core::Job& job)
{
	auto& commandBuffer = device.BeginSingleTimeCommands();

    job.commandBuffer = &commandBuffer;
    job.Execute();
    
    device.EndSingleTimeCommands(commandBuffer);
}

void Core::CommandBuffer::ImmediateSubmit(Device& device, vector<Job*>& jobs)
{
    auto& commandBuffer = device.BeginSingleTimeCommands();

	for (auto& job : jobs)
	{
		job->commandBuffer = &commandBuffer;
		job->Execute();
	}

    device.EndSingleTimeCommands(commandBuffer);
}

void Core::CommandBuffer::GetAccessAndStageMask(const VkImageLayout& inImageLayout, VkAccessFlags& outAccessFlags, VkPipelineStageFlags& outPipelineStageFlags)
{
    switch (inImageLayout)
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

VkPipelineStageFlags Core::CommandBuffer::SanitizeStageMask(VkPipelineStageFlags stageMask) const
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

void Core::CommandBuffer::DrawIndexedIndirect(Buffer& indirectBuffer, uint32_t drawCount, uint32_t stride)
{
	if (drawCount == 0)
		return;

	vkCmdDrawIndexedIndirect(
		_commandBuffer,
		indirectBuffer.GetBuffer(),
		0,
		drawCount,
		stride
	);
}

void Core::CommandBuffer::FillBuffer(Buffer& buffer, VkDeviceSize offset, VkDeviceSize size, uint32_t data)
{
	vkCmdFillBuffer(_commandBuffer, buffer.GetBuffer(), offset, size, data);
}

void Core::CommandBuffer::Barrier(
	VkPipelineStageFlags srcStageMask,
	VkPipelineStageFlags dstStageMask,
	VkAccessFlags srcAccessMask,
	VkAccessFlags dstAccessMask)
{
	VkMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = srcAccessMask;
	barrier.dstAccessMask = dstAccessMask;

	vkCmdPipelineBarrier(
		_commandBuffer,
		srcStageMask,
		dstStageMask,
		0,
		1, &barrier,
		0, nullptr,
		0, nullptr);
}

void Core::CommandBuffer::BufferBarrier(
	Buffer& buffer,
	VkPipelineStageFlags srcStageMask,
	VkPipelineStageFlags dstStageMask,
	VkAccessFlags srcAccessMask,
	VkAccessFlags dstAccessMask,
	QueueType destQueue)
{
	VkBufferMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	barrier.srcAccessMask = srcAccessMask;
	barrier.dstAccessMask = dstAccessMask;
	
    if (destQueue == QueueType::None)
	{
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	}
	else
	{
		const auto& qfi = _device.GetQueueFamilyIndices();

        if (destQueue == QueueType::Graphics)
        {
            barrier.srcQueueFamilyIndex = qfi.ComputeFamily.value();
            barrier.dstQueueFamilyIndex = qfi.GraphicsFamily.value();
        }
        else
        {
            barrier.srcQueueFamilyIndex = qfi.GraphicsFamily.value();
            barrier.dstQueueFamilyIndex = qfi.ComputeFamily.value();
        }
	}

	barrier.buffer = buffer.GetBuffer();
	barrier.offset = 0;
	barrier.size = VK_WHOLE_SIZE;

	vkCmdPipelineBarrier(
		_commandBuffer,
        SanitizeStageMask(srcStageMask),
        SanitizeStageMask(dstStageMask),
		0,
		0, nullptr,
		1, &barrier,
		0, nullptr);
}

void Core::CommandBuffer::BeginDebugMarker(const char* markerName, float r, float g, float b, float a)
{
	const float color[4] = { r, g, b, a };
	_device.GetDebugUtils().BeginLabel(_commandBuffer, markerName, color);
}

void Core::CommandBuffer::EndDebugMarker()
{
	_device.GetDebugUtils().EndLabel(_commandBuffer);
}

void Core::CommandBuffer::InsertDebugMarker(const char* markerName, float r, float g, float b, float a)
{
	const float color[4] = { r, g, b, a };
	_device.GetDebugUtils().InsertLabel(_commandBuffer, markerName, color);
}

void Core::CommandBuffer::BindDescriptorSet(VkPipelineBindPoint pipelineBindPoint,
	Shader& shader,
	DescriptorSetResources& resources)
{
	if (resources.descriptorSet == VK_NULL_HANDLE)
		return;

	auto pipelineLayout = shader.GetPipelineLayout();

	vkCmdBindDescriptorSets(
		_commandBuffer, pipelineBindPoint,
		pipelineLayout, resources.setIndex, 1,
		&resources.descriptorSet, 0, nullptr);
}