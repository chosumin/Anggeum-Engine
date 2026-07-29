#pragma once
#include "Vulkans/DescriptorPool.h"
#include "Vulkans/MemoryAllocator.h"
#include "ResourcePool.h"
#include "FrameResources.h"
#include "RenderExecutor.h"
#include "RenderScene.h"
#include "Vulkans/SubmitInfo.h"

namespace Core
{
	class BindlessTextureManager;
	class DescriptorSetBuilder;
	class RendererBatch;
	class RenderScene;

	class RenderFrame
	{
	public:
		// Primary frames receive the shared GPU-driven managers by reference.
		RenderFrame(Device& device, RenderScene& renderScene);
		// Temporary frames (e.g. IBL prefilter) draw without the GPU-driven managers.
		explicit RenderFrame(Device& device);
		~RenderFrame();
		
		// Reset frame resources
		void Reset();

		// Submit info management
		SubmitInfo& AddSubmitInfo(QueueType queueType, VkCommandBuffer commandBuffer, SyncContext& syncContext);
		SubmitInfo& GetCurrentSubmitInfo() { return _submission.submitInfos.back(); }
		FrameSubmission& GetSubmission() { return _submission; }

		BindlessTextureManager* GetBindlessTextureManager() const { return _renderScene.GetBindlessTextureManager(); }
		bool HasBindlessSupport() const { return _renderScene.HasBindlessSupport(); }
		DescriptorSetResources* GetBindlessResources();

		// Always present on scene frames (temp frames must not call these).
		MeshBufferManager& GetMeshBufferManager() const { return *_renderScene.GetMeshBufferManager(); }

		MaterialManager& GetMaterialManager() { return *_renderScene.GetMaterialManager(); }

		// Per-frame GPU resources (render targets, transient buffers, framebuffers)
		// live in FrameResources. Passes reach them through here.
		FrameResources& GetResources() { return _resources; }
		const FrameResources& GetResources() const { return _resources; }

		// Culler management - per camera and RendererBatch, reused within a frame
		RenderExecutor& GetRenderExecutor() { return *_renderExecutor; }

		// RendererBatch is owned by RenderContext and shared across frames-in-flight.
		RendererBatch& GetRendererBatch() const { return *_renderScene.GetRendererBatch(); }
	private:
		void CreateSyncObjects();

	private:
		Device& _device;

		FrameSubmission _submission;

		// Non-owning: the RenderScene owned by Engine. Temp frames bind this to a shared
		// empty instance (all managers null).
		RenderScene& _renderScene;

		DescriptorSetResources _bindlessResources;

		// Per-frame GPU resources (render targets / transient buffers / framebuffers)
		FrameResources _resources;

		// Per-camera/RendererBatch cullers, reused within a frame
		unique_ptr<RenderExecutor> _renderExecutor;
	};
}