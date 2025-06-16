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

		vector<CommandBuffer> Begin();

		void Submit(CommandBuffer& commandBuffer, CommandBuffer& computeBuffer);

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
		void EndFrame(VkSemaphore* semaphore);
		void SubmitComputeBuffer(CommandBuffer& computeBuffer);
	private:
		Device& _device;

		SwapChain* _swapChain;
		//vector<unique_ptr<RenderFrame>> _frames;

		CommandPool* _commandPool;
		CommandPool* _computeCommandPool;

		uint32_t _currentFrame;
		uint32_t _imageIndex;
		u64 _lastComputeSemaphoreValue;
		u32 _maxFramesInFlight = MAX_FRAMES_IN_FLIGHT - 1;

		vector<VkSemaphore> _imageAvailableSemaphores;
		vector<VkSemaphore> _renderFinishedSemaphores;

		VkSemaphore _graphicsSemaphore;
		VkSemaphore _computeSemaphore;
	};
}
