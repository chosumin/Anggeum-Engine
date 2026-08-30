#pragma once
#include "ResourceHandle.h"
#include "SyncContext.h"
#include "GpuQueueTimer.h"
#include "RenderScene.h"
#include "FrameCounter.h"

namespace Core
{
	class ResourceManager;
	/*
	 * RenderContext acts as a frame manager
	 * It swaps between RenderFrame objects and forwards a request for vulkan resources to the active frame.
	 * More than one frame can be in-flight in the GPU, thus the need for per-frame resources.
	 */
	class CommandBuffer;
	class SwapChain;
	class CommandPool;
	class RenderFrame;
	class BindlessTextureManager;
	class RendererBatch;
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
		RenderContext(Device& device, ResourceManager& resourceManager,
			RenderScene& renderScene, SyncContext& syncContext);
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

		// Swap chain
		SwapChain& GetSwapChain() const;
		VkExtent2D GetSurfaceExtent() const;

		// GPU-driven rendering managers (grouped in RenderScene, owned by Engine)
		BindlessTextureManager* GetBindlessTextureManager() const { return _renderScene.GetBindlessTextureManager(); }
		bool HasBindlessSupport() const { return _renderScene.HasBindlessSupport(); }
		MeshBufferManager* GetMeshBufferManager() const { return _renderScene.GetMeshBufferManager(); }
		MaterialManager* GetMaterialManager() const { return _renderScene.GetMaterialManager(); }
		RendererBatch* GetRendererBatch() const { return _renderScene.GetRendererBatch(); }

		Handle<Texture> GetPreviousFrameDepth() const { return _previousFrameDepth; }

		SyncContext& GetSyncContext() { return _syncContext; }
	private:
		void CreateRenderFrames();
		void AcquireSwapChainAndResetFence(SwapChain& swapChain);
		void EndFrame(VkSemaphore* semaphore);
		
	private:
		Device& _device;
		ResourceManager& _resourceManager;
		
		SwapChain* _swapChain = nullptr;
		uint32_t _imageIndex = 0;
		
		// Serves only the per-frame resource-init primary. 
		CommandPool* _initResourceCommandPool = nullptr;

		// Per-frame resources
		vector<unique_ptr<RenderFrame>> _frames;
		uint32_t _currentFrame = 0;
		
		// Sync primitives (timeline semaphores, timeline values, frame snapshots)
		SyncContext& _syncContext;
		array<FrameTimelineSnapshot, MAX_FRAMES_IN_FLIGHT> _frameSnapshots;

		// GPU-side measurement of how much the two queues actually overlap
		unique_ptr<GpuQueueTimer> _queueTimer;
		QueueTimings _lastQueueTimings;
		double _lastQueueSubmitMs = 0.0;
		double _lastPresentMs = 0.0;

		// GPU mirror of the scene, owned by Engine and borrowed here (shared by
		// reference with every frame-in-flight). RenderContext only reads it for
		// rendering.
		RenderScene& _renderScene;

		// Double/Triple buffered depth
		array<Handle<Texture>, MAX_FRAMES_IN_FLIGHT> _frameDepthBuffers;
		Handle<Texture> _previousFrameDepth;
	};
}
