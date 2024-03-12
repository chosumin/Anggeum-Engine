#pragma once

namespace Core
{
	class Device;
	class CommandBuffer;
	class CommandPool
	{
	public:
		CommandPool(Device& device,
			uint32_t queueFamilyIndex,
			size_t threadIndex = 0);
		virtual ~CommandPool();

		VkCommandPool& GetHandle() { return _commandPool; }

		size_t GetThreadIndex() const { return _threadIndex; }

		CommandBuffer& RequestCommandBuffer(uint32_t currentFrame);
		void ResetCommandBuffers(uint32_t currentFrame);
	private:
	private:
		Device& _device;
		VkCommandPool _commandPool;

		uint32_t _queueFamilyIndex;
		size_t _threadIndex;

		vector<unique_ptr<CommandBuffer>> _commandBuffers;
	};
}

