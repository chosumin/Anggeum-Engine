#pragma once

namespace Core
{
	class Device;
	class CommandBuffer;
	class SyncContext;
	enum class QueueType;

	class CommandPool
	{
	public:
		// Timeline-recycled pool
		CommandPool(Device& device, SyncContext& sync, QueueType queue);

		// Unbound pool (blocking one-shot use)
		CommandPool(Device& device, QueueType queue);

		virtual ~CommandPool();

		VkCommandPool& GetHandle() { return _commandPool; }

		uint32_t GetQueueFamilyIndex() const { return _queueFamilyIndex; }

		CommandBuffer& RequestCommandBuffer(VkCommandBufferLevel level);
	private:
		Device& _device;
		VkCommandPool _commandPool;

		uint32_t _queueFamilyIndex;

		// Fixed at construction; null = unbound (one-shot) pool.
		SyncContext* _sync = nullptr;
		QueueType _timelineQueue{};

		vector<unique_ptr<CommandBuffer>> _primaryCommandBuffers;
		vector<unique_ptr<CommandBuffer>> _secondaryCommandBuffers;
	};
}

