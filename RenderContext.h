#pragma once

namespace Core
{
	class CommandBuffer;
	class SwapChain;
	class RenderContext
	{
	public:
		static void AddResizeCallback(function<void(SwapChain&)>);
		static void RemoveResizeCallback(function<void(SwapChain&)>);
	private:
		static vector<function<void(SwapChain&)>> _resizeCallbacks;
	public:
		RenderContext(Device& device);

		RenderContext(const RenderContext&) = delete;
		RenderContext(RenderContext&&) = delete;

		virtual ~RenderContext();

		RenderContext& operator=(const RenderContext&) = delete;
		RenderContext& operator=(RenderContext&&) = delete;

		void Prepare(size_t threadCount = 1);

		void RecreateSwapChain();

		CommandBuffer& Begin();
		CommandBuffer& GetCommandBuffer() 
		{
			return *_commandBuffers[_currentFrame];
		}

		void Submit(CommandBuffer& commandBuffer);
		void Submit(const vector<CommandBuffer*>& commandBuffers);

		void BeginFrame();

		SwapChain& GetSwapChain() const;
		VkExtent2D GetSurfaceExtent() const;
		uint32_t GetCurrentFrame() { return _currentFrame; }

		VkCommandPool GetCommandPool() { return _commandPool; }

		uint32_t GetImageIndex() const { return _imageIndex; }
		//RenderFrame& GetActiveFrame();
		//uint32_t GetActiveFrameIndex();
		//RenderFrame& GetLastRenderedFrame();

		//VkSemaphore RequestSemaphore();
		//VkSemaphore RequestSemaphoreWithOwnership();
		//void ReleaseOwnedSemaphore(VkSemaphore semaphore);

		//VkSemaphore ConsumeAcquiredSemaphore();
	private:
		void CreateCommandPool();
		void CreateCommandBuffers();
		void CreateSyncObjects();

		void AcquireSwapChainAndResetCommandBuffer(SwapChain& swapChain);
		void EndFrame(SwapChain& swapChain);
	private:
		Device& _device;

		SwapChain* _swapChain;
		//vector<unique_ptr<RenderFrame>> _frames;
		uint32_t _currentFrame = 0;
		bool _frameActive{ false };

		size_t _threadCount{ 1 };

		VkCommandPool _commandPool;
		vector<CommandBuffer*> _commandBuffers;

		uint32_t _imageIndex;
		vector<VkSemaphore> _imageAvailableSemaphores;
		vector<VkSemaphore> _renderFinishedSemaphores;
		vector<VkFence> _inFlightFences;
	};
}
