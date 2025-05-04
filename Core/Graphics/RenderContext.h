#pragma once

namespace Core
{
	/*
	 * RenderContext acts as a frame manager
	 * It swaps between RenderFrame objects and forwards a request for vulkan resources to the active frame.
	 * More than one frame can be in-flight in the GPU, thus the need for per-frame resources.
	 */
	class FrameCounter
	{
	public:
		static void IncreaseFrame() { ++_frameNumber; }
		static uint64_t GetFrameNumber() { return _frameNumber; }
	private:
		static inline uint64_t _frameNumber = 0;
	};

	class CommandBuffer;
	class SwapChain;
	class CommandPool;
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

		void Submit(CommandBuffer& commandBuffer);

		void BeginFrame();

		SwapChain& GetSwapChain() const;
		VkExtent2D GetSurfaceExtent() const;

		uint32_t GetCurrentFrame() { return _currentFrame; }

		uint32_t GetImageIndex() const { return _imageIndex; }
		//RenderFrame& GetActiveFrame();
		//uint32_t GetActiveFrameIndex();
		//RenderFrame& GetLastRenderedFrame();

		//VkSemaphore RequestSemaphore();
		//VkSemaphore RequestSemaphoreWithOwnership();
		//void ReleaseOwnedSemaphore(VkSemaphore semaphore);

		//VkSemaphore ConsumeAcquiredSemaphore();
	private:
		void CreateSyncObjects();

		void AcquireSwapChainAndResetFence(SwapChain& swapChain);
		void Submit(const vector<CommandBuffer*>& commandBuffers);
		void EndFrame(VkSemaphore* semaphore);
	private:
		Device& _device;

		SwapChain* _swapChain;
		//vector<unique_ptr<RenderFrame>> _frames;

		uint32_t _currentFrame = 0;

		CommandPool* _commandPool;

		uint32_t _imageIndex;
		vector<VkSemaphore> _imageAvailableSemaphores;
		vector<VkSemaphore> _renderFinishedSemaphores;
		vector<VkFence> _inFlightFences;
	};
}
