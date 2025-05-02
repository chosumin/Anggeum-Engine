#include "stdafx.h"
#include "CommandPool.h"
#include "CommandBuffer.h"

namespace Core
{
	CommandPool::CommandPool(Device& device, uint32_t queueFamilyIndex)
		:_device(device), _queueFamilyIndex(queueFamilyIndex)
	{
		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = queueFamilyIndex;

		if (vkCreateCommandPool(device.GetDevice(), &poolInfo, nullptr, &_commandPool) != VK_SUCCESS)
			throw runtime_error("failed to create command pool!");
	}

	CommandPool::~CommandPool()
	{
		_primaryCommandBuffers.clear();
		_secondaryCommandBuffers.clear();

		vkDestroyCommandPool(_device.GetDevice(), _commandPool, nullptr);
	}

	CommandBuffer& CommandPool::RequestCommandBuffer(
		uint32_t currentFrame, VkCommandBufferLevel level)
	{
		auto& buffers = level == VK_COMMAND_BUFFER_LEVEL_PRIMARY ?
			_primaryCommandBuffers : _secondaryCommandBuffers;

		if (currentFrame < buffers.size())
		{
			buffers[currentFrame]->ResetCommandBuffer();
			return *buffers[currentFrame];
		}

		buffers.emplace_back(make_unique<CommandBuffer>(_device, *this, level));

		return *buffers.back();
	}

	CommandBuffer& CommandPool::GetCommandBuffer(
		uint32_t currentFrame, VkCommandBufferLevel level)
	{
		auto& buffers = level == VK_COMMAND_BUFFER_LEVEL_PRIMARY ?
			_primaryCommandBuffers : _secondaryCommandBuffers;

		return *buffers[currentFrame];
	}
}
