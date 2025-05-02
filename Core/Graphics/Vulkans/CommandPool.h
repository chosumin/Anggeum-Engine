#pragma once

namespace Core
{
	class Device;
	class CommandBuffer;
	class CommandPool
	{
	public:
		CommandPool(Device& device, uint32_t queueFamilyIndex, VkCommandBufferLevel level);
		virtual ~CommandPool();

		VkCommandPool& GetHandle() { return _commandPool; }

		CommandBuffer& RequestCommandBuffer(uint32_t currentFrame);
		void ResetCommandBuffers(uint32_t currentFrame);

		CommandBuffer& GetCommandBuffer(uint32_t currentFrame);
	private:
		Device& _device;
		VkCommandPool _commandPool;

		uint32_t _queueFamilyIndex;

		VkCommandBufferLevel _level;

		vector<unique_ptr<CommandBuffer>> _commandBuffers;
	};
}

