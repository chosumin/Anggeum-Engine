#include "stdafx.h"
#include "CommandBuffer.h"
#include "Pipeline.h"
#include "SwapChain.h"
#include "Shader.h"
#include "Buffer.h"
#include "Texture.h"
#include "Image.h"
#include "BindlessTextureManager.h"
#include "DescriptorPool.h"
#include "Graphics/FrameCounter.h"
#include "Graphics/RenderFrame.h"
#include "Foundation/Job.h"

Core::CommandBuffer::CommandBuffer(Device& device, CommandPool& commandPool, VkCommandBufferLevel level)
	:_device(device), _level(level)
{
	_frame = Core::FrameCounter::GetFrameNumber();
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

void Core::CommandBuffer::BeginCommandBuffer(VkCommandBufferUsageFlags flags)
{
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = flags;

	// Secondary command buffers are only recorded outside render/rendering
	// scopes (transfer jobs), so the inheritance info carries no render pass.
	VkCommandBufferInheritanceInfo inheritanceInfo = {};
	if (_level == VK_COMMAND_BUFFER_LEVEL_SECONDARY)
	{
		inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
		beginInfo.pInheritanceInfo = &inheritanceInfo;
	}

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


void Core::CommandBuffer::BeginRendering(const RenderingSetup& setup)
{
    vkCmdBeginRendering(_commandBuffer, &setup.Finalize());
}

void Core::CommandBuffer::EndRendering()
{
    vkCmdEndRendering(_commandBuffer);
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

void Core::CommandBuffer::PushConstantsInternal(Shader& shader, uint32_t index, const void* data, uint32_t size)
{
	auto& pushConstantRanges = shader.GetPushConstantRanges();

	assert(index < pushConstantRanges.size());

	// sizeof(T) may exceed the range declared in the shader: C++ pads a struct up
	// to its alignment, GLSL does not. Only require that the caller supplies at
	// least the bytes the shader actually reads.
	assert(size >= pushConstantRanges[index].size);

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

void Core::CommandBuffer::CopyBuffer(Buffer& srcBuffer, Buffer& dstBuffer,
    VkDeviceSize dstOffset, VkDeviceSize srcOffset, VkDeviceSize size)
{
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = (size == 0) ? srcBuffer.GetSize() : size;

    vkCmdCopyBuffer(_commandBuffer, srcBuffer.GetBuffer(), dstBuffer.GetBuffer(),
        1, &copyRegion);
}

void Core::CommandBuffer::CopyImage(Texture& srcTexture, Texture& dstTexture,
    uint32_t srcMipLevel, uint32_t srcLayer, uint32_t dstMipLevel, uint32_t dstLayer)
{
    Image& srcImage = srcTexture.GetImage();
    Image& dstImage = dstTexture.GetImage();

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

void Core::CommandBuffer::CopyBufferToImage(Buffer& buffer, Texture& texture, uint32_t width, uint32_t height,
    VkDeviceSize bufferOffset)
{
    Image& image = texture.GetImage();

    VkBufferImageCopy region{};
    region.bufferOffset = bufferOffset;
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

void Core::CommandBuffer::CopyBufferToImage(Buffer& buffer, Texture& texture,
    const vector<VkBufferImageCopy>& regions)
{
    assert(!regions.empty());

    vkCmdCopyBufferToImage(
        _commandBuffer,
        buffer.GetBuffer(),
        texture.GetImage().GetImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<uint32_t>(regions.size()),
        regions.data()
    );
}

void Core::CommandBuffer::CopyImageToBuffer(Texture& texture, VkImageLayout layout,
    Buffer& buffer, uint32_t width, uint32_t height)
{
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { width, height, 1 };

    vkCmdCopyImageToBuffer(
        _commandBuffer,
        texture.GetImage().GetImage(),
        layout,
        buffer.GetBuffer(),
        1,
        &region
    );
}

void Core::CommandBuffer::GenerateMipmaps(Texture& texture, uint32_t mipLevels)
{
    Image& image = texture.GetImage();

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


void Core::CommandBuffer::EndCommandBuffer()
{
    if (vkEndCommandBuffer(_commandBuffer) != VK_SUCCESS)
        throw std::runtime_error("failed to record command buffer!");
}

bool Core::CommandBuffer::IsBusy()
{
    // A buffer used at frame N may still be executing on the GPU until its frame
    // slot has cycled through every frame in flight.
    return Core::FrameCounter::GetFrameNumber() < _frame + MAX_FRAMES_IN_FLIGHT;
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

void Core::CommandBuffer::DrawIndexedIndirectCount(Buffer& indirectBuffer,
	VkDeviceSize indirectOffset, Buffer& countBuffer, VkDeviceSize countOffset,
	uint32_t maxDrawCount, uint32_t stride)
{
	assert(_device.SupportsDrawIndirectCount()
		&& "drawIndirectCount feature is not available on this device");

	vkCmdDrawIndexedIndirectCount(
		_commandBuffer,
		indirectBuffer.GetBuffer(),
		indirectOffset,
		countBuffer.GetBuffer(),
		countOffset,
		maxDrawCount,
		stride
	);
}

void Core::CommandBuffer::DispatchIndirect(Buffer& argsBuffer, VkDeviceSize offset)
{
	vkCmdDispatchIndirect(_commandBuffer, argsBuffer.GetBuffer(), offset);
}

void Core::CommandBuffer::FillBuffer(Buffer& buffer, VkDeviceSize offset, VkDeviceSize size, uint32_t data)
{
	vkCmdFillBuffer(_commandBuffer, buffer.GetBuffer(), offset, size, data);
}

Core::BarrierBatch Core::CommandBuffer::CreateBarrierBatch()
{
	return BarrierBatch(*this, _device, _queueFamilyIndex);
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