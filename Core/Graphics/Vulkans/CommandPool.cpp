#include "stdafx.h"
#include "CommandPool.h"
#include "CommandBuffer.h"

namespace Core
{
	CommandPool::CommandPool(Device& device,
		uint32_t queueFamilyIndex,
		size_t threadIndex)
		:_device(device), _queueFamilyIndex(queueFamilyIndex),
		_threadIndex(threadIndex)
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
		_commandBuffers.clear();

		vkDestroyCommandPool(_device.GetDevice(), _commandPool, nullptr);
	}

	CommandBuffer& CommandPool::RequestCommandBuffer(uint32_t currentFrame)
	{
		if (currentFrame < _commandBuffers.size())
		{
			return *_commandBuffers[currentFrame];
		}

		_commandBuffers.emplace_back(make_unique<CommandBuffer>(_device, *this));

		return *_commandBuffers.back();
	}

	void CommandPool::ResetCommandBuffers(uint32_t currentFrame)
	{
		if (currentFrame < _commandBuffers.size())
			_commandBuffers[currentFrame]->ResetCommandBuffer();
	}
}
