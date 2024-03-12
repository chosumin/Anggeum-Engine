#include "stdafx.h"
#include "CommandPool.h"
#include "CommandBuffer.h"

namespace Core
{
	CommandPool::CommandPool(Device& device,
		uint32_t queueFamilyIndex,
		size_t threadIndex = 0)
		:_device(device), _queueFamilyIndex(queueFamilyIndex),
		_threadIndex(threadIndex)
	{
		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = queueFamilyIndex;

		auto device = Device::Instance().GetDevice();
		if (vkCreateCommandPool(device.GetDevice(), &poolInfo, nullptr, &_commandPool) != VK_SUCCESS)
			throw runtime_error("failed to create command pool!");
	}

	CommandPool::~CommandPool()
	{
		_commandBuffers.clear();

		vkDestroyCommandPool(_device.GetDevice(), _commandPool, nullptr);
	}

	CommandBuffer& CommandPool::RequestCommandBuffer()
	{
		if (activeCommandBufferCount < _commandBuffers.size())
		{
			return *_commandBuffers[activeCommandBufferCount++];
		}

		//todo : use MAX_FRAMES_IN_FLIGHT;
		_commandBuffers.emplace_back(make_unique<CommandBuffer>(*this));

		activeCommandBufferCount++;

		return *_commandBuffers.back();
	}

	void CommandPool::ResetCommandBuffers()
	{
		VkResult result = VK_SUCCESS;

		for (auto& commandBuffer : _commandBuffers)
		{
			commandBuffer->ResetCommandBuffer();
		}

		activeCommandBufferCount = 0;
	}
}
