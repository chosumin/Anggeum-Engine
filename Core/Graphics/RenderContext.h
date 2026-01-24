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
	class RenderFrame;
	class BindlessTextureManager; // Added
	
	class RenderContext
	{
	public:
		static void AddResizeCallback(function<void(SwapChain&)>);
		static void RemoveResizeCallback(function<void(SwapChain&)>);
	private:
		static vector<function<void(SwapChain&)>> _resizeCallbacks;
	public:
		RenderContext(Device& device);
		~RenderContext();
		
		void Prepare(size_t threadCount);
		void RecreateSwapChain();
		
		// Frame management
		void Begin(); // Allocate and start command buffers
		void Submit();
		
		// Get current frame
		RenderFrame& GetCurrentFrame() { return *_frames[_currentFrame]; }
		uint32_t GetCurrentFrameIndex() const { return _currentFrame; }
		uint32_t GetImageIndex() const { return _imageIndex; }
		
		// Swap chain
		SwapChain& GetSwapChain() const;
		VkExtent2D GetSurfaceExtent() const;

		// Added: Bindless texture manager access
		BindlessTextureManager* GetBindlessTextureManager() const { return _bindlessTextureManager.get(); }
		bool HasBindlessSupport() const { return _bindlessTextureManager != nullptr; }

	private:
		void CreateRenderFrames();
		void CreateSyncObjects();
		void AcquireSwapChainAndResetFence(SwapChain& swapChain);
		void SubmitComputeBuffer();
		void EndFrame(VkSemaphore* semaphore);
		
	private:
		Device& _device;
		
		// Swap chain
		SwapChain* _swapChain = nullptr;
		uint32_t _imageIndex = 0;
		
		// Command pools (owned by RenderContext)
		CommandPool* _commandPool = nullptr;
		CommandPool* _computeCommandPool = nullptr;
		
		// Per-frame resources
		vector<unique_ptr<RenderFrame>> _frames; // MAX_FRAMES_IN_FLIGHT count
		uint32_t _currentFrame = 0;
		
		// Timeline semaphores
		VkSemaphore _graphicsSemaphore = VK_NULL_HANDLE;
		VkSemaphore _computeSemaphore = VK_NULL_HANDLE;
		u64 _lastComputeSemaphoreValue = 0;
		u32 _maxFramesInFlight = MAX_FRAMES_IN_FLIGHT;

		// Global bindless texture manager
		unique_ptr<BindlessTextureManager> _bindlessTextureManager;
	};
}
