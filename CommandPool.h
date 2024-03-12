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

		CommandBuffer& RequestCommandBuffer();
		void ResetCommandBuffers();
	private:
	private:
		Device& _device;
		VkCommandPool _commandPool;

		uint32_t _queueFamilyIndex;
		size_t _threadIndex = 0;

		uint32_t activeCommandBufferCount = 0;
		vector<unique_ptr<CommandBuffer>> _commandBuffers;
	};
}

