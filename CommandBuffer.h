#pragma once

namespace Core
{
	class Pipeline;
	class SwapChain;
	class UniformBuffer;
	class Polygon;
	class CommandBuffer
	{
	public:
		CommandBuffer();
		~CommandBuffer();

		void RecordCommandBuffer(Pipeline& pipeline, SwapChain& swapChain, UniformBuffer& uniformBuffer, Polygon& polygon);
		void EndFrame(Pipeline& pipeline, SwapChain& swapChain);
		VkCommandPool GetCommandPool() { return _commandPool; }
		uint32_t GetCurrentFrame() { return _currentFrame; }
		VkCommandBuffer GetCommandBuffer() { return _commandBuffers[_currentFrame]; }
	private:
		void CreateCommandPool();
		void CreateCommandBuffers();
		void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex,
			Pipeline& pipeline, SwapChain& swapChain, Polygon& polygon);
		void EndCommandBuffer(VkCommandBuffer commandBuffer);
		void CreateSyncObjects();
	private:
		VkCommandPool _commandPool;
		vector<VkCommandBuffer> _commandBuffers;

		vector<VkSemaphore> _imageAvailableSemaphores;
		vector<VkSemaphore> _renderFinishedSemaphores;
		vector<VkFence> _inFlightFences;

		uint32_t _currentFrame = 0;
		uint32_t _imageIndex;
	};
}

