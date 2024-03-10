#pragma once

namespace Core
{
	class CommandBuffer;
	class SwapChain;
	class RenderContext
	{
	public:
		RenderContext(Device& device);

		RenderContext(const RenderContext&) = delete;
		RenderContext(RenderContext&&) = delete;

		virtual ~RenderContext();

		RenderContext& operator=(const RenderContext&) = delete;
		RenderContext& operator=(RenderContext&&) = delete;

		void Prepare(size_t threadCount = 1);

		void Recreate();
		void RecreateSwapChain();

		CommandBuffer& Begin();
		CommandBuffer& GetCommandBuffer() 
		{
			return *_commandBuffer;
		}

		void Submit(CommandBuffer& commandBuffer);
		void Submit(const vector<CommandBuffer*>& commandBuffers);

		void BeginFrame();

		SwapChain& GetSwapChain() const;
		VkExtent2D GetSurfaceExtent() const;
		uint32_t GetActiveFrameIndex() const;

		//RenderFrame& GetActiveFrame();
		//uint32_t GetActiveFrameIndex();
		//RenderFrame& GetLastRenderedFrame();

		//VkSemaphore RequestSemaphore();
		//VkSemaphore RequestSemaphoreWithOwnership();
		//void ReleaseOwnedSemaphore(VkSemaphore semaphore);

		//VkSemaphore ConsumeAcquiredSemaphore();
	private:
		Device& _device;

		SwapChain* _swapChain;
		CommandBuffer* _commandBuffer;

		//vector<unique_ptr<RenderFrame>> _frames;
		VkSemaphore _acquiredSemaphore;
		bool _prepared{ false };
		uint32_t _activeFrameIndex{ 0 };
		bool _frameActive{ false };

		size_t _threadCount{ 1 };
	};
}
