#pragma once

namespace Core
{
	class Device;
	class CommandBuffer;
	class CommandPool
	{
	public:
		CommandPool(Device& device, uint32_t queueFamilyIndex);
		virtual ~CommandPool();

		VkCommandPool& GetHandle() { return _commandPool; }

		CommandBuffer& RequestCommandBuffer(VkCommandBufferLevel level);
	private:
		Device& _device;
		VkCommandPool _commandPool;

		uint32_t _queueFamilyIndex;

		vector<unique_ptr<CommandBuffer>> _primaryCommandBuffers;
		vector<unique_ptr<CommandBuffer>> _secondaryCommandBuffers;
	};
}

