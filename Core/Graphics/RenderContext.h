#pragma once
#include "MeshBufferManager.h"
#include "MaterialManager.h"
#include "SyncContext.h"
#include "GpuQueueTimer.h"

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
	class BindlessTextureManager;
	class Texture;
	class Scene;
	class SyncContext;

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

		void RecreateSwapChain();

		// Frame management
		void Begin(Scene& scene, VkExtent2D extents);
		void Submit();
		
		// Get current frame
		RenderFrame& GetCurrentFrame() { return *_frames[_currentFrame]; }
		uint32_t GetCurrentFrameIndex() const { return _currentFrame; }

		GpuQueueTimer& GetQueueTimer() { return *_queueTimer; }

		// Graphics/compute overlap measured on the GPU. Lags by MAX_FRAMES_IN_FLIGHT
		// frames, since a slot's timestamps are only readable once its work is done.
		const QueueTimings& GetLastQueueTimings() const { return _lastQueueTimings; }

		// Submit() split into its two halves. Present usually dominates when it does,
		// and that is the CPU blocking on frame pacing rather than doing work.
		double GetLastQueueSubmitMs() const { return _lastQueueSubmitMs; }
		double GetLastPresentMs() const { return _lastPresentMs; }
		uint32_t GetImageIndex() const { return _imageIndex; }

		// Command buffer allocation
		CommandBuffer& RequestCommandBuffer();
		CommandBuffer& RequestComputeCommandBuffer();
		
		// Swap chain
		SwapChain& GetSwapChain() const;
		VkExtent2D GetSurfaceExtent() const;

		// Bindless texture manager
		BindlessTextureManager* GetBindlessTextureManager() const { return _bindlessTextureManager.get(); }
		bool HasBindlessSupport() const { return _bindlessTextureManager != nullptr; }

		// Managers
		MeshBufferManager* GetMeshBufferManager() const { return _meshBufferManager.get(); }
		MaterialManager* GetMaterialManager() const { return _materialManager.get(); }

		shared_ptr<Texture> GetPreviousFrameDepth() const { return _previousFrameDepth; }

		SyncContext& GetSyncContext() { return *_syncContext; }
	private:
		void CreateRenderFrames();
		void AcquireSwapChainAndResetFence(SwapChain& swapChain);
		void EndFrame(VkSemaphore* semaphore);
		
	private:
		Device& _device;
		
		// Swap chain
		SwapChain* _swapChain = nullptr;
		uint32_t _imageIndex = 0;
		
		// Command pools
		CommandPool* _commandPool = nullptr;
		CommandPool* _computeCommandPool = nullptr;
		
		// Per-frame resources
		vector<unique_ptr<RenderFrame>> _frames;
		uint32_t _currentFrame = 0;
		
		// Sync primitives (timeline semaphores, timeline values, frame snapshots)
		unique_ptr<SyncContext> _syncContext;
		array<FrameTimelineSnapshot, MAX_FRAMES_IN_FLIGHT> _frameSnapshots;

		// GPU-side measurement of how much the two queues actually overlap
		unique_ptr<GpuQueueTimer> _queueTimer;
		QueueTimings _lastQueueTimings;
		double _lastQueueSubmitMs = 0.0;
		double _lastPresentMs = 0.0;

		unique_ptr<BindlessTextureManager> _bindlessTextureManager;

		// GPU Driven Rendering managers
		unique_ptr<MeshBufferManager> _meshBufferManager;
		unique_ptr<MaterialManager> _materialManager;

		// Double/Triple buffered depth
		array<shared_ptr<Texture>, MAX_FRAMES_IN_FLIGHT> _frameDepthBuffers;
		shared_ptr<Texture> _previousFrameDepth;
	};
}
