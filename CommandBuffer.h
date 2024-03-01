#pragma once

namespace Core
{
	class Pipeline;
	class SwapChain;
	class Shader;
	class Buffer;
	class CommandBuffer
	{
	public:
		static void AddResizeCallback(function<void(SwapChain&)>);
		static void RemoveResizeCallback(function<void(SwapChain&)>);
	public:
		CommandBuffer();
		~CommandBuffer();

		void BeginCommandBuffer();
		void BeginRenderPass(VkRenderPassBeginInfo renderPassInfo);
		void BindPipeline(VkPipelineBindPoint pipelineBindPoint,
			VkPipeline pipeline);
		void SetViewportAndScissor(VkViewport viewport, VkRect2D scissor);
		void BindDescriptorSets(VkPipelineBindPoint pipelineBindPoint,
			VkPipelineLayout pipelineLayout, VkDescriptorSet* descriptorSet);
		void Flush(Shader& shader);
		void BindVertexBuffers(Buffer& buffer);
		void BindIndexBuffer(Buffer& buffer, VkIndexType indexType);
		void DrawIndexed(uint32_t indexCount, uint32_t instanceCount);
		void RecordCommandBuffer(SwapChain& swapChain);
		void EndFrame(SwapChain& swapChain);
		VkCommandPool GetCommandPool() { return _commandPool; }
		uint32_t GetCurrentFrame() { return _currentFrame; }
		VkCommandBuffer GetCommandBuffer() { return _commandBuffers[_currentFrame]; }

		uint32_t GetImageIndex() const 
		{
			return _imageIndex;
		}
	private:
		void CreateCommandPool();
		void CreateCommandBuffers();
		void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex,
			Pipeline& pipeline, SwapChain& swapChain);
		void EndCommandBuffer(VkCommandBuffer commandBuffer);
		void CreateSyncObjects();
	private:
		static vector<function<void(SwapChain&)>> _resizeCallbacks;
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

